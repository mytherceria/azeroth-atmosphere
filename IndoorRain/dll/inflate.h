/* inflate.h: zlib (RFC 1950) and deflate (RFC 1951) decoding for IndoorRain.dll, written for the MPQ
 * reader (mpq.c), which hands it one archive sector at a time.
 *
 * One call decodes one whole zlib stream from memory into memory, and it is strict, because the stream
 * comes from a file on the player's disk that anything could have written: the output must come out at
 * exactly the length the caller expects (no byte past it is ever written, and a stream that stops short
 * is an error), the Adler-32 at the end must match, and nothing may follow it. No allocation, no
 * recursion, no C runtime; every read is bounded by the input's length and every write by the output's.
 */
#ifndef INDOORRAIN_INFLATE_H
#define INDOORRAIN_INFLATE_H

#define INFL_OK            0
#define INFL_E_HEADER    (-1)   /* not a zlib header: not deflate, a window over 32 KB, bad check bits, or a preset dictionary */
#define INFL_E_TRUNCATED (-2)   /* the input ends inside the stream */
#define INFL_E_BLOCK     (-3)   /* block type 3, or a stored block whose length and its complement disagree */
#define INFL_E_CODES     (-4)   /* a code description that is over-subscribed, incomplete, too long, or has no end-of-block */
#define INFL_E_SYMBOL    (-5)   /* bits that are no code, or a length or distance symbol deflate does not define */
#define INFL_E_DISTANCE  (-6)   /* a match reaching back before the start of the output */
#define INFL_E_OVERFLOW  (-7)   /* more output than the expected length */
#define INFL_E_SHORT     (-8)   /* the stream ends before the expected length */
#define INFL_E_CHECKSUM  (-9)   /* the Adler-32 does not match the output */
#define INFL_E_TRAILING  (-10)  /* bytes after the Adler-32 */

#define INFL_FAST_BITS 9        /* codes this long or shorter decode with one table lookup, longer ones a bit at a time */

/* One canonical Huffman code: how many codes there are of each length, the symbols in code order, and a
   table on the next INFL_FAST_BITS input bits (entry = symbol << 4 | code length; 0 = no code that short
   starts with those bits). */
typedef struct infl_code {
    unsigned short count[16];
    unsigned short symbol[288];
    unsigned short fast[1 << INFL_FAST_BITS];
} infl_code;

/* Everything one decode works in, about 5.6 KB. The caller provides it (mpq.c allocates one per archive
   read) instead of this code putting it on the stack: the DLL is built without stack probes
   (-mno-stack-arg-probe), and a thread's stack only grows one 4 KB guard page at a time. Its fields are
   private to inflate.c and need no initialising. */
typedef struct infl_state {
    const unsigned char *in; unsigned in_len, in_pos;
    unsigned bitbuf, bitcnt;       /* input bits read but not used yet, the next one lowest */
    unsigned char *out; unsigned out_len, out_pos;
    unsigned short lens[288 + 32]; /* code lengths of the block being set up */
    infl_code lencode, distcode, clcode;
} infl_state;

/* Decode the zlib stream in[0..in_len) into out[0..out_len). INFL_OK only when the stream is complete and
   well formed, produced exactly out_len bytes, its Adler-32 matches, and it ends exactly at in_len; anything
   else returns an INFL_E_ code, and whatever is in out is not to be used. */
int infl_zlib(infl_state *st, const unsigned char *in, unsigned in_len, unsigned char *out, unsigned out_len);
/* A short English name for an INFL_ code, for logs. */
const char *infl_error_text(int code);

#endif
