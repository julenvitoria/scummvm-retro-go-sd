/*
 * Resource packs: what a SCUMM v7 data file too big for the external flash (Full Throttle's
 * FT.LA1: 152 MB, The Dig's DIG.LA1) holds besides its sounds, copied once into a file of its
 * own next to it on the card -- <file>.gwpack, ~28 MB for FT -- which is then cached in the
 * flash like the game's other files.
 *
 * The engine still reads the data file on the card; the stream it reads it through (src/
 * gw_system.cpp) serves every read that falls inside a packed block from the flash instead,
 * and gwDirectResource() hands out the block where it lies, so rooms, costumes (FT's run to
 * 550 KB each), scripts and charsets are used in place, as v4-v6 resources are from a cached
 * data file. The sounds (120 MB in FT) stay on the card: the engine streams them
 * (patches/scummvm-slim.patch, imuse_digi).
 *
 * Layout (little endian):
 *   pack_head_t
 *   pack_entry_t[count]   sorted by 'src': where each block starts in the data file
 *   the blocks, each from its 8-byte header (tag, size) on, at 4-byte aligned offsets
 *
 * A pack is rebuilt when its header does not match the data file (size, a checksum of its
 * first 4 KB -- the room offsets table), or when the file was cut short.
 */
#include "platform.h"
#include "pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_MAGIC   0x4b505747u            /* "GWPK" */
#define PACK_VERSION 1u
#define COPY_CHUNK   (16u << 10)

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int read_at(void *f, uint32_t off, void *buf, uint32_t n)
{
	return plat_fseek(f, (int32_t)off, 0) && plat_fread(buf, n, f) == n;
}

/* the data file's fingerprint: its size and the bytes of its first 4 KB */
static int fingerprint(void *f, uint32_t size, uint32_t *sum)
{
	static uint8_t buf[4096];
	uint32_t n = size < sizeof buf ? size : sizeof buf, s = 0x9e3779b9u;
	if (!read_at(f, 0, buf, n)) return 0;
	for (uint32_t i = 0; i < n; i++) s = (s ^ buf[i]) * 16777619u;
	*sum = s;
	return 1;
}

int svm_pack_candidate(const char *path, uint32_t size)
{
	uint8_t h[8];
	void *f = plat_fopen(path, "rb");
	if (!f) return 0;
	int ok = plat_fread(h, 8, f) == 8 && !memcmp(h, "LECF", 4) && be32(h + 4) == size;
	plat_fclose(f);
	return ok;
}

int svm_pack_valid(const pack_head_t *h, uint32_t pack_size, const char *src, uint32_t src_size)
{
	if (pack_size < sizeof *h || h->magic != PACK_MAGIC || h->version != PACK_VERSION ||
	    h->src_size != src_size || h->total != pack_size || !h->count ||
	    h->data_off != sizeof *h + h->count * sizeof(pack_entry_t))
		return 0;
	void *f = plat_fopen(src, "rb");
	if (!f) return 0;
	uint32_t sum = 0;
	int ok = fingerprint(f, src_size, &sum) && sum == h->src_sum;
	plat_fclose(f);
	return ok;
}

int svm_pack_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct))
{
	pack_head_t h;
	pack_entry_t *e = NULL;
	uint8_t *buf = NULL;
	void *in = NULL, *out = NULL;
	uint32_t count = 0, cap = 0, total = 0;
	int ok = 0;

	memset(&h, 0, sizeof h);
	in = plat_fopen(src, "rb");
	if (!in || !fingerprint(in, src_size, &h.src_sum)) goto done;

	/* the blocks: every child of every LFLF (one room's resources) but its sounds */
	uint8_t hd[8];
	uint32_t o = 8;
	while (o + 8 <= src_size) {
		if (!read_at(in, o, hd, 8)) goto done;
		uint32_t sz = be32(hd + 4);
		if (sz < 8 || o + sz > src_size) break;
		if (!memcmp(hd, "LFLF", 4)) {
			uint32_t p = o + 8;
			while (p + 8 <= o + sz) {
				if (!read_at(in, p, hd, 8)) goto done;
				uint32_t s = be32(hd + 4);
				if (s < 8 || p + s > o + sz) break;
				if (memcmp(hd, "SOUN", 4)) {
					if (count == cap) {
						cap = cap ? cap * 2 : 512;
						pack_entry_t *ne = (pack_entry_t *)realloc(e, cap * sizeof *e);
						if (!ne) goto done;
						e = ne;
					}
					e[count].src = p;
					e[count].size = s;
					count++;
				}
				p += s;
			}
		}
		o += sz;
	}
	if (!count) goto done;

	h.magic = PACK_MAGIC;
	h.version = PACK_VERSION;
	h.src_size = src_size;
	h.count = count;
	h.data_off = sizeof h + count * sizeof *e;
	total = h.data_off;
	for (uint32_t i = 0; i < count; i++) {
		e[i].pack = total;
		total += (e[i].size + 3) & ~3u;
	}
	h.total = total;

	buf = (uint8_t *)malloc(COPY_CHUNK);
	out = plat_fopen(dst, "wb");
	if (!buf || !out) goto done;
	/* the header goes in last: a pack cut short (card pulled, battery) is never taken as whole */
	pack_head_t blank;
	memset(&blank, 0, sizeof blank);
	if (plat_fwrite(&blank, sizeof blank, out) != sizeof blank ||
	    plat_fwrite(e, count * sizeof *e, out) != count * sizeof *e) goto done;
	uint32_t done_bytes = 0, step = total / 50 + 1, next = 0;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t left = e[i].size, at = e[i].src;
		if (!plat_fseek(in, (int32_t)at, 0)) goto done;
		while (left) {
			uint32_t n = left < COPY_CHUNK ? left : COPY_CHUNK;
			if (plat_fread(buf, n, in) != n || plat_fwrite(buf, n, out) != n) goto done;
			left -= n;
			done_bytes += n;
		}
		uint32_t pad = ((e[i].size + 3) & ~3u) - e[i].size;
		static const uint8_t zero[4];
		if (pad && plat_fwrite(zero, pad, out) != pad) goto done;
		if (progress && done_bytes >= next) {
			progress((int)((uint64_t)done_bytes * 100 / total));
			next = done_bytes + step;
		}
	}
	if (!plat_fseek(out, 0, 0) || plat_fwrite(&h, sizeof h, out) != sizeof h) goto done;
	ok = 1;
done:
	if (out) plat_fclose(out);
	if (in) plat_fclose(in);
	free(buf);
	free(e);
	return ok;
}

const pack_entry_t *svm_pack_find(const pack_head_t *h, uint32_t pos, uint32_t n)
{
	const pack_entry_t *e = (const pack_entry_t *)(h + 1);
	uint32_t lo = 0, hi = h->count;
	while (lo < hi) {                        /* the last entry starting at or before pos */
		uint32_t mid = (lo + hi) / 2;
		if (e[mid].src <= pos) lo = mid + 1;
		else hi = mid;
	}
	if (!lo) return NULL;
	const pack_entry_t *k = &e[lo - 1];
	return pos + n <= k->src + k->size ? k : NULL;
}
