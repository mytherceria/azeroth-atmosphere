/* inflate.c: zlib (RFC 1950) and deflate (RFC 1951) decoding for IndoorRain.dll; see inflate.h.
 *
 * Written from the two RFCs for this DLL (no zlib source in it). The decode is the textbook one: a bit
 * buffer filled a byte at a time, canonical Huffman codes rebuilt for every block, and a byte-by-byte
 * copy for matches. A code of up to INFL_FAST_BITS bits is found with one lookup in a table on the next
 * input bits; a longer one is walked a bit at a time, which RFC 1951 3.2.2 makes simple, since the codes
 * of one length are consecutive integers. The source archives use 4 KB sectors, each its own stream,
 * so the tables are rebuilt often; building one costs less than decoding a few hundred bytes with it.
 *
 * Every error is final and reported, never repaired: this decodes files a player's disk holds, and a
 * sound built from a guess at damaged data is worse than no sound. Tested against zlib's own deflate, and
 * with truncated, bit-flipped and hand-built bad streams, in tools/mpq_selftest.c.
 */
#include "inflate.h"

/* RFC 1951 3.2.5: base value and extra bits of the length symbols 257..285 and the distance symbols 0..29 */
static const unsigned short INFL_LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char INFL_LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short INFL_DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char INFL_DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
/* RFC 1951 3.2.7: the order a dynamic block sends the lengths of its code length code in */
static const unsigned char INFL_CL_ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* ---- bits ---- */
/* Top the bit buffer up to more than 24 bits while the input lasts, a byte at a time. Deflate packs bits
   from the least significant end, so each new byte goes above the bits held, and the buffer's bits above
   bitcnt are always zero. */
static void infl_refill(infl_state *s)
{
    while (s->bitcnt <= 24 && s->in_pos < s->in_len) { s->bitbuf |= (unsigned)s->in[s->in_pos++] << s->bitcnt; s->bitcnt += 8; }
}
/* Whether the next n bits (n <= 15) exist; infl_take then removes them. */
static int infl_need(infl_state *s, unsigned n) { if (s->bitcnt < n) infl_refill(s); return s->bitcnt >= n; }
static unsigned infl_take(infl_state *s, unsigned n) { unsigned v = s->bitbuf & ((1u << n) - 1u); s->bitbuf >>= n; s->bitcnt -= n; return v; }
/* To the next byte boundary, for a stored block or the Adler-32, which are read as bytes from in_pos. The
   buffer holds the rest of a partly used byte (dropped here) and whole bytes read ahead, which go back. */
static void infl_align(infl_state *s) { s->in_pos -= s->bitcnt >> 3; s->bitbuf = 0; s->bitcnt = 0; }

/* ---- codes ---- */
enum { INFL_CL, INFL_LITLEN, INFL_DIST };
/* Build code h from the code lengths of symbols 0..n-1 (RFC 1951 3.2.2). An over-subscribed set is always
   refused. An incomplete one passes only in the two cases RFC 1951 3.2.7 describes, as zlib does: a single
   code of one bit, and (distances only) no codes at all, for a block of literals. Any other incomplete set
   leaves bit patterns that mean nothing, which no honest compressor writes. */
static int infl_build(infl_code *h, const unsigned short *lens, unsigned n, int kind)
{
    unsigned short next[16];
    unsigned len, sym, code, i;
    int left = 1;
    for (len = 0; len < 16; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) { if (lens[sym] > 15) return INFL_E_CODES; h->count[lens[sym]]++; }
    for (len = 1; len < 16; len++) { left = left * 2 - h->count[len]; if (left < 0) return INFL_E_CODES; }
    if (left > 0) {
        if (kind == INFL_CL) return INFL_E_CODES;
        if (h->count[0] == n) { if (kind != INFL_DIST) return INFL_E_CODES; }
        else if (h->count[1] != 1 || h->count[0] != n - 1) return INFL_E_CODES;
    }
    /* the symbols in code order: shorter codes first, and within one length by symbol value */
    next[1] = 0;
    for (len = 1; len < 15; len++) next[len + 1] = (unsigned short)(next[len] + h->count[len]);
    for (sym = 0; sym < n; sym++) if (lens[sym]) h->symbol[next[lens[sym]]++] = (unsigned short)sym;
    /* the lookup table: a short code fills every slot whose low bits are that code as it arrives. Huffman
       codes are sent most significant bit first into a stream read from the least significant end, so
       the slot is the code with its bits reversed. */
    for (i = 0; i < (1u << INFL_FAST_BITS); i++) h->fast[i] = 0;
    code = 0; i = 0;
    for (len = 1; len <= INFL_FAST_BITS; len++, code <<= 1) {
        unsigned k;
        for (k = 0; k < h->count[len]; k++, code++, i++) {
            unsigned rev = 0, b, slot;
            for (b = 0; b < len; b++) rev |= ((code >> b) & 1u) << (len - 1u - b);
            for (slot = rev; slot < (1u << INFL_FAST_BITS); slot += 1u << len) h->fast[slot] = (unsigned short)(h->symbol[i] << 4 | len);
        }
    }
    return INFL_OK;
}
/* The next symbol of code h, or a negative INFL_E_ code. */
static int infl_decode(infl_state *s, const infl_code *h)
{
    unsigned e, len, code, first, index, bits;
    infl_refill(s);
    e = h->fast[s->bitbuf & ((1u << INFL_FAST_BITS) - 1u)];
    if (e) {
        len = e & 15u;
        if (len > s->bitcnt) return INFL_E_TRUNCATED;   /* the lookup matched on the zeros past the end of the input */
        s->bitbuf >>= len; s->bitcnt -= len;
        return (int)(e >> 4);
    }
    /* no short code starts here: walk a longer one bit by bit. The codes of each length are the consecutive
       integers from first, so the bits read so far are a code when they fall in [first, first + count). */
    code = first = index = 0; bits = s->bitbuf;
    for (len = 1; len < 16; len++) {
        if (len > s->bitcnt) return INFL_E_TRUNCATED;
        code |= bits & 1u; bits >>= 1;
        if (code - first < h->count[len]) { s->bitbuf >>= len; s->bitcnt -= len; return h->symbol[index + (code - first)]; }
        index += h->count[len]; first = (first + h->count[len]) << 1; code <<= 1;
    }
    return INFL_E_SYMBOL;
}

/* ---- blocks ---- */
/* The symbols of one Huffman-coded block, up to its end-of-block symbol (RFC 1951 3.2.5). */
static int infl_codes(infl_state *s)
{
    for (;;) {
        int sym = infl_decode(s, &s->lencode);
        unsigned len, dist, extra;
        if (sym < 0) return sym;
        if (sym < 256) {
            if (s->out_pos >= s->out_len) return INFL_E_OVERFLOW;
            s->out[s->out_pos++] = (unsigned char)sym;
            continue;
        }
        if (sym == 256) return INFL_OK;
        sym -= 257;
        if (sym >= 29) return INFL_E_SYMBOL;   /* 286 and 287 have codes in the fixed code but no meaning */
        extra = INFL_LEN_EXTRA[sym];
        if (!infl_need(s, extra)) return INFL_E_TRUNCATED;
        len = INFL_LEN_BASE[sym] + infl_take(s, extra);
        sym = infl_decode(s, &s->distcode);
        if (sym < 0) return sym;
        if (sym >= 30) return INFL_E_SYMBOL;   /* 30 and 31 likewise */
        extra = INFL_DIST_EXTRA[sym];
        if (!infl_need(s, extra)) return INFL_E_TRUNCATED;
        dist = INFL_DIST_BASE[sym] + infl_take(s, extra);
        if (dist > s->out_pos) return INFL_E_DISTANCE;   /* no preset dictionary, so nothing lies before the output */
        if (len > s->out_len - s->out_pos) return INFL_E_OVERFLOW;
        {   /* a byte at a time, forwards: a match may overlap itself (distance 1 repeats one byte len times) */
            unsigned char *to = s->out + s->out_pos;
            const unsigned char *from = to - dist;
            s->out_pos += len;
            while (len--) *to++ = *from++;
        }
    }
}
/* A stored block: LEN, its one's complement NLEN, then LEN bytes as they are (RFC 1951 3.2.4). */
static int infl_stored(infl_state *s)
{
    const unsigned char *p;
    unsigned len, i;
    infl_align(s);
    if (s->in_len - s->in_pos < 4) return INFL_E_TRUNCATED;
    p = s->in + s->in_pos;
    len = p[0] | (unsigned)p[1] << 8;
    if ((p[2] | (unsigned)p[3] << 8) != (~len & 0xFFFFu)) return INFL_E_BLOCK;
    s->in_pos += 4; p += 4;
    if (len > s->in_len - s->in_pos) return INFL_E_TRUNCATED;
    if (len > s->out_len - s->out_pos) return INFL_E_OVERFLOW;
    for (i = 0; i < len; i++) s->out[s->out_pos + i] = p[i];
    s->out_pos += len; s->in_pos += len;
    return INFL_OK;
}
/* A block with the fixed codes of RFC 1951 3.2.6. All 288 literal/length and all 32 distance codes are
   built, so both codes are complete; the four symbols that mean nothing are refused when they turn up. */
static int infl_fixed(infl_state *s)
{
    unsigned i;
    int err;
    for (i = 0; i < 288; i++) s->lens[i] = (unsigned short)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    if ((err = infl_build(&s->lencode, s->lens, 288, INFL_LITLEN)) != INFL_OK) return err;
    for (i = 0; i < 32; i++) s->lens[i] = 5;
    if ((err = infl_build(&s->distcode, s->lens, 32, INFL_DIST)) != INFL_OK) return err;
    return infl_codes(s);
}
/* A block that describes its own codes (RFC 1951 3.2.7): the counts, the code length code, the code lengths
   of both codes sent with it (runs of the previous length or of zeros included), then the data. */
static int infl_dynamic(infl_state *s)
{
    unsigned nlen, ndist, ncode, i;
    int err;
    if (!infl_need(s, 14)) return INFL_E_TRUNCATED;
    nlen = infl_take(s, 5) + 257; ndist = infl_take(s, 5) + 1; ncode = infl_take(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return INFL_E_CODES;   /* the fields can say 288 and 32; deflate defines 286 and 30 */
    for (i = 0; i < 19; i++) s->lens[i] = 0;
    for (i = 0; i < ncode; i++) { if (!infl_need(s, 3)) return INFL_E_TRUNCATED; s->lens[INFL_CL_ORDER[i]] = (unsigned short)infl_take(s, 3); }
    if ((err = infl_build(&s->clcode, s->lens, 19, INFL_CL)) != INFL_OK) return err;
    i = 0;
    while (i < nlen + ndist) {   /* one sequence: a run may carry on from the last literal/length into the distances */
        int sym = infl_decode(s, &s->clcode);
        unsigned rep, val;
        if (sym < 0) return sym;
        if (sym < 16) { s->lens[i++] = (unsigned short)sym; continue; }
        if (sym == 16) {
            if (i == 0) return INFL_E_CODES;   /* a repeat with nothing before it */
            if (!infl_need(s, 2)) return INFL_E_TRUNCATED;
            val = s->lens[i - 1]; rep = 3 + infl_take(s, 2);
        } else if (sym == 17) {
            if (!infl_need(s, 3)) return INFL_E_TRUNCATED;
            val = 0; rep = 3 + infl_take(s, 3);
        } else {
            if (!infl_need(s, 7)) return INFL_E_TRUNCATED;
            val = 0; rep = 11 + infl_take(s, 7);
        }
        if (rep > nlen + ndist - i) return INFL_E_CODES;   /* a run past the last length */
        while (rep--) s->lens[i++] = (unsigned short)val;
    }
    if (s->lens[256] == 0) return INFL_E_CODES;   /* no end-of-block code: the block could never end */
    if ((err = infl_build(&s->lencode, s->lens, nlen, INFL_LITLEN)) != INFL_OK) return err;
    if ((err = infl_build(&s->distcode, s->lens + nlen, ndist, INFL_DIST)) != INFL_OK) return err;
    return infl_codes(s);
}

/* ---- the stream ---- */
/* RFC 1950 8.2. 5552 bytes is the most that can be summed before b could pass 32 bits. */
static unsigned infl_adler32(const unsigned char *p, unsigned n)
{
    unsigned a = 1, b = 0;
    while (n) {
        unsigned k = n < 5552u ? n : 5552u;
        n -= k;
        while (k--) { a += *p++; b += a; }
        a %= 65521u; b %= 65521u;
    }
    return b << 16 | a;
}
int infl_zlib(infl_state *s, const unsigned char *in, unsigned in_len, unsigned char *out, unsigned out_len)
{
    unsigned last, type, want;
    int err;
    s->in = in; s->in_len = in_len; s->in_pos = 0; s->bitbuf = 0; s->bitcnt = 0;
    s->out = out; s->out_len = out_len; s->out_pos = 0;
    /* RFC 1950 2.2: method 8 (deflate), a window of at most 32 KB, check bits making CMF and FLG a multiple
       of 31, and no preset dictionary (MPQ writers never use one, and there is none to give) */
    if (in_len < 2) return INFL_E_TRUNCATED;
    if ((in[0] & 15u) != 8u || (in[0] >> 4) > 7u || ((unsigned)in[0] << 8 | in[1]) % 31u != 0u || (in[1] & 0x20u)) return INFL_E_HEADER;
    s->in_pos = 2;
    do {
        if (!infl_need(s, 3)) return INFL_E_TRUNCATED;
        last = infl_take(s, 1); type = infl_take(s, 2);
        if (type == 0) err = infl_stored(s);
        else if (type == 1) err = infl_fixed(s);
        else if (type == 2) err = infl_dynamic(s);
        else err = INFL_E_BLOCK;
        if (err != INFL_OK) return err;
    } while (!last);
    infl_align(s);
    if (s->out_pos != s->out_len) return INFL_E_SHORT;
    if (s->in_len - s->in_pos < 4) return INFL_E_TRUNCATED;
    want = (unsigned)in[s->in_pos] << 24 | (unsigned)in[s->in_pos + 1] << 16 | (unsigned)in[s->in_pos + 2] << 8 | in[s->in_pos + 3];
    s->in_pos += 4;
    if (infl_adler32(out, out_len) != want) return INFL_E_CHECKSUM;
    /* Every sector in the client's archives ends exactly at its Adler-32. Bytes after it mean the sector table
       and the data disagree about where a sector ends, and which one is right would be a guess. */
    if (s->in_pos != s->in_len) return INFL_E_TRAILING;
    return INFL_OK;
}

const char *infl_error_text(int code)
{
    switch (code) {
    case INFL_OK:          return "ok";
    case INFL_E_HEADER:    return "not a zlib stream header";
    case INFL_E_TRUNCATED: return "stream cut short";
    case INFL_E_BLOCK:     return "bad block type or stored length";
    case INFL_E_CODES:     return "bad Huffman code description";
    case INFL_E_SYMBOL:    return "invalid code in the data";
    case INFL_E_DISTANCE:  return "match reaches before the start";
    case INFL_E_OVERFLOW:  return "more data than expected";
    case INFL_E_SHORT:     return "less data than expected";
    case INFL_E_CHECKSUM:  return "Adler-32 mismatch";
    case INFL_E_TRAILING:  return "bytes after the end of the stream";
    }
    return "unknown inflate error";
}
