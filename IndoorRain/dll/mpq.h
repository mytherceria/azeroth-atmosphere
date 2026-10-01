/* mpq.h: reads one file out of one of the game client's MPQ archives into memory, for IndoorRain.dll,
 * which rebuilds its sounds at run time from the player's own client files. See mpq.c for what it accepts
 * and why. No global state: calls from several threads at once are safe when the callbacks are.
 */
#ifndef INDOORRAIN_MPQ_H
#define INDOORRAIN_MPQ_H

/* The caller's file and memory access (Win32 file calls in the DLL, stdio and malloc in the self-test).
   alloc must return memory aligned as malloc's is. read_at may return fewer bytes than asked only at the
   end of the file or on an error; the reader treats any short read as an error. */
typedef struct mpq_io {
  void *(*alloc)(void *ctx, unsigned size);   /* NULL on failure */
  void  (*release)(void *ctx, void *p);
  void *(*open)(void *ctx, const char *archive);   /* archive file name inside the Data folder, e.g. "patch-S.mpq"; case-insensitive on the caller's side; NULL if missing */
  unsigned (*read_at)(void *ctx, void *fh, unsigned long long off, void *buf, unsigned len);   /* bytes actually read */
  unsigned long long (*size)(void *ctx, void *fh);
  void  (*close)(void *ctx, void *fh);
  void *ctx;
} mpq_io;

#define MPQ_OK              0
#define MPQ_E_NOT_FOUND   (-1)   /* the archive has no such file */
#define MPQ_E_NO_ARCHIVE  (-2)   /* io->open found no such archive */
#define MPQ_E_EMPTY       (-3)   /* the archive holds the file with a size of zero: a stub, or a delete marker */
#define MPQ_E_ARG         (-4)   /* a NULL argument, callback or list entry, an empty path or one over 1023 characters, n < 0 */
#define MPQ_E_IO          (-5)   /* a read inside the archive came back short */
#define MPQ_E_FORMAT      (-6)   /* no MPQ header in the first 1 MB, or a header or table that does not fit the archive */
#define MPQ_E_CORRUPT     (-7)   /* the file's block entry or sector table does not fit its block, its size or the archive */
#define MPQ_E_DATA        (-8)   /* a compressed sector that does not inflate to exactly its size, or fails its checksum */
#define MPQ_E_UNSUPPORTED (-9)   /* refused, never guessed at: MPQ format 2 and up, an archive over 4 GB, an encrypted,
                                    imploded or patch file, any compression but zlib, a flag this reader does not know */
#define MPQ_E_TOO_BIG     (-10)  /* the file is over 64 MB */
#define MPQ_E_NOMEM       (-11)  /* io->alloc returned NULL */

/* path is the name inside the archive, e.g. "Sound\\Spells\\CallLightning.wav"; case does not matter and '/'
   counts as '\\', as in the client. */

/* One file from one named archive. 0 = ok (*out allocated with io->alloc, caller releases), negative = error code; a missing file is its own code.
   On any error *out is NULL and *out_len 0, and everything the call allocated or opened is released. A file
   stored with size zero is MPQ_E_EMPTY, so MPQ_OK always hands over at least one byte. */
int mpq_read(const mpq_io *io, const char *archive, const char *path, unsigned char **out, unsigned *out_len);

/* First archive in the list that holds the file with a NON-ZERO size (zero-length stubs exist and must be skipped).
   Skipped: an archive that is not there (MPQ_E_NO_ARCHIVE), one without the file (MPQ_E_NOT_FOUND) and one
   holding it with size zero (MPQ_E_EMPTY). Any other error stops the search and is returned, with *which set
   to that archive: the first archive holding the file decides, and an older copy further down the list is
   never used in its place. *which is the archive's index in the list, or -1 (which may be NULL). When
   nothing is found, the result is MPQ_E_EMPTY if any archive held a stub, else MPQ_E_NOT_FOUND if any
   archive opened, else MPQ_E_NO_ARCHIVE. */
int mpq_read_first(const mpq_io *io, const char *const *archives, int n, const char *path, unsigned char **out, unsigned *out_len, int *which);

/* A short English name for an MPQ_ code, for logs. */
const char *mpq_error_text(int code);

#endif
