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
	uint32_t reserved;
} pack_head_t;

typedef struct {
	uint32_t src;                 /* where the block (its 8-byte header) is in the data file */
	uint32_t size;                /* the block's size, header included */
	uint32_t pack;                /* where it is in the pack */
} pack_entry_t;

int svm_pack_candidate(const char *path, uint32_t size);   /* a v7 data file ("LECF")? */
int svm_pack_valid(const pack_head_t *h, uint32_t pack_size, const char *src, uint32_t src_size);
int svm_pack_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct));
/* the entry holding bytes [pos, pos + n) of the data file, or NULL */
const pack_entry_t *svm_pack_find(const pack_head_t *h, uint32_t pos, uint32_t n);

#ifdef __cplusplus
}
#endif
