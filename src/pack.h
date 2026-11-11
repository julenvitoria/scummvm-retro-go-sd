/* Resource packs of SCUMM v7 data files too big for the flash: src/pack.c */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint32_t magic, version;
	uint32_t src_size, src_sum;   /* the data file it was made from */
	uint32_t count, data_off;     /* entries; where the blocks start */
	uint32_t total;               /* the pack's size */
	uint32_t reserved;            /* PACK_SCUMM7 (0 in the packs made before there were kinds) or PACK_SKY */
} pack_head_t;

typedef struct {
	uint32_t src;                 /* where the block (its 8-byte header) is in the data file */
	uint32_t size;                /* the block's size, header included */
	uint32_t pack;                /* where it is in the pack */
} pack_entry_t;

#define PACK_SCUMM7 1                 /* a SCUMM v7 data file's blocks but its sounds */
#define PACK_SKY    2                 /* Beneath a Steel Sky's sky.dsk: its files but the speech, unpacked */
int svm_pack_candidate(const char *path, uint32_t size);   /* 0, or the kind of pack this file gets */
int svm_pack_valid(const pack_head_t *h, uint32_t pack_size, const char *src, uint32_t src_size);
int svm_pack_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct));
/* the entry holding bytes [pos, pos + n) of the data file, or NULL */
/* PACK_SKY: file number nr */
const pack_entry_t *svm_pack_file(const pack_head_t *h, uint32_t nr);
const pack_entry_t *svm_pack_find(const pack_head_t *h, uint32_t pos, uint32_t n);

/* A pack goes into the flash in pieces of at most SVM_PACK_PIECE bytes, each where the flash
   has room for it: 38 MB in one piece needed 38 MB free in a row, which a flash with the core
   copied to its middle does not have. A piece starts at an entry, so an entry is never split.
   starts[0] = 0 (the header and the table are in the first piece); returns the number of
   pieces (0: the table could not be read, or more than cap pieces). f: the pack, open. */
#define SVM_PACK_PIECE (8u << 20)
#define SVM_PACK_MAX_PIECES 12
int svm_pack_split(void *f, const pack_head_t *h, uint32_t *starts, int cap);
/* where offset off of the pack is in memory (plat_map_pack()'s pack: platform_*.c) */
const void *plat_pack_ptr(const pack_head_t *h, uint32_t off);

#ifdef __cplusplus
}
#endif

/* A file kept in the flash XORed with a fixed byte whatever its contents, by name: Soltys'
   vol.dat (CGE, every byte ^ 0xA5), so that its sprites are drawn from the flash. 0: none. */
static inline uint8_t svm_fixed_key(const char *name)
{
	const char *b = name;
	for (const char *q = name; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
	static const char want[] = "vol.dat";
	for (int i = 0; ; i++) {
		char c = b[i];
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c != want[i]) return 0;
		if (!c) return 0xA5;
	}
}
