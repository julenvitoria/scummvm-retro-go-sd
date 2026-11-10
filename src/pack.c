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
#include <strings.h>

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

/* "sky.dsk" -> "sky.dnr", in the same case */
static int sky_dnr_path(const char *dsk, char *out, size_t cap)
{
	size_t n = strlen(dsk);
	if (n < 7 || n + 1 > cap || strcasecmp(dsk + n - 7, "sky.dsk")) return 0;
	memcpy(out, dsk, n + 1);
	int up = dsk[n - 1] == 'K';
	out[n - 2] = up ? 'N' : 'n';
	out[n - 1] = up ? 'R' : 'r';
	return 1;
}

int svm_pack_candidate(const char *path, uint32_t size)
{
	char dnr[320];
	if (sky_dnr_path(path, dnr, sizeof dnr)) {      /* Beneath a Steel Sky's data file */
		void *d = plat_fopen(dnr, "rb");
		if (!d) return 0;
		plat_fclose(d);
		return PACK_SKY;
	}
	uint8_t h[8];
	void *f = plat_fopen(path, "rb");
	if (!f) return 0;
	int ok = plat_fread(h, 8, f) == 8 && !memcmp(h, "LECF", 4) && be32(h + 4) == size;
	plat_fclose(f);
	return ok ? PACK_SCUMM7 : 0;
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

static int sky_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct));

int svm_pack_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct))
{
	char dnr[320];
	if (sky_dnr_path(src, dnr, sizeof dnr)) return sky_build(src, src_size, dst, progress);
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

/* ------------------------------------------------------------------ Beneath a Steel Sky
 *
 * sky.dsk (72 MB with speech) holds 5099 files, most of them packed (RNC ProPack, method 1);
 * the engine loads each one whole, unpacked: animations of up to 1 MB, sprite sets of 580 KB.
 * Its pack holds every file but the speech (numbers 50000-58999: small, read from the card),
 * unpacked, exactly as Disk::loadFile() would hand it out: ~37 MB, used where it lies.
 *
 * The entries' 'src' is the file's number, 'size' what loadFile() returns for it.
 */

static uint32_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* RNC method 1, after ScummVM's common/compression/rnc_deco.cpp, but neither the packed file
   (up to 400 KB) nor the unpacked one (up to 1 MB) is ever whole in memory: the input comes
   through an 8 KB buffer and the output goes through a 64 KB window (matches reach back 32 KB) */
#define RNC_IN (8u << 10)
typedef struct {
	uint8_t *ibuf;                /* RNC_IN bytes; 'src' is ibuf + ipos */
	uint32_t ipos, ihave, ifile;  /* ifile: bytes of the packed file still on the card */
	void *inf;
	int32_t left;
	uint16_t bl, bh;
	uint8_t bc;
	uint16_t raw[64], pos[64], len[64];
	uint8_t *win;                 /* 65536 bytes */
	uint32_t out;                 /* bytes unpacked so far */
	void *f;                      /* where they go */
	uint16_t crc;
	int err;
} rnc_t;

static uint16_t g_crc[256];

static void rnc_crc_init(void)
{
	if (g_crc[1]) return;
	for (unsigned i = 0; i < 256; i++) {
		uint16_t t = (uint16_t)i;
		for (int k = 0; k < 8; k++) t = (t & 1) ? (uint16_t)((t >> 1) ^ 0xa001) : (uint16_t)(t >> 1);
		g_crc[i] = t;
	}
}

static void rnc_flush(rnc_t *r, uint32_t n)       /* the last n bytes: one piece of the window */
{
	uint32_t at = (r->out - n) & 0xffff;
	if (n && plat_fwrite(r->win + at, n, r->f) != n) r->err = 1;
}

static inline void rnc_put(rnc_t *r, uint8_t b)
{
	r->win[r->out & 0xffff] = b;
	r->crc = (uint16_t)(g_crc[(r->crc ^ b) & 0xff] ^ (r->crc >> 8));
	if ((++r->out & 0x3fff) == 0) rnc_flush(r, 0x4000);
}

/* at least k bytes (k <= 4096) at the input position, when the file has that many left */
static const uint8_t *rnc_need(rnc_t *r, uint32_t k)
{
	if (r->ihave - r->ipos < k && r->ifile) {
		uint32_t tail = r->ihave - r->ipos;
		memmove(r->ibuf, r->ibuf + r->ipos, tail);
		uint32_t n = RNC_IN - 8 - tail;
		if (n > r->ifile) n = r->ifile;
		if (plat_fread(r->ibuf + tail, n, r->inf) != n) r->err = 1;
		r->ifile -= n;
		r->ipos = 0;
		r->ihave = tail + n;
		memset(r->ibuf + r->ihave, 0, 8);
	}
	return r->ibuf + r->ipos;
}

static uint16_t rnc_bits(rnc_t *r, uint8_t amount)
{
	uint32_t h = r->bh, l = r->bl, rem;
	int c = r->bc;
	uint16_t ret = (uint16_t)(((1u << amount) - 1) & l);
	c -= amount;
	if (c < 0) {
		c += amount;
		rem = (h << (16 - c)) & 0xffff;
		h >>= c;
		l >>= c;
		l |= rem;
		r->ipos += 2;
		r->left -= 2;
		const uint8_t *src = rnc_need(r, 2);
		if (r->left <= 0) h = 0;
		else if (r->left == 1) h = *src;
		else h = le16(src);
		amount = (uint8_t)(amount - c);
		c = 16 - amount;
	}
	rem = (h << (16 - amount)) & 0xffff;
	r->bh = (uint16_t)(h >> amount);
	r->bl = (uint16_t)((l >> amount) | rem);
	r->bc = (uint8_t)c;
	return ret;
}

static void rnc_table(rnc_t *r, uint16_t *t)
{
	uint16_t n = rnc_bits(r, 5);
	if (!n) return;
	if (n > 16) { r->err = 1; return; }
	uint8_t len[16];
	for (unsigned i = 0; i < n; i++) len[i] = (uint8_t)rnc_bits(r, 4);
	uint16_t code = 0;
	for (unsigned bl = 1; bl < 17; bl++)
		for (unsigned i = 0; i < n; i++)
			if (len[i] == bl) {
				*t++ = (uint16_t)((1u << bl) - 1);
				uint16_t b = (uint16_t)(code >> (16 - bl)), a = 0;
				for (unsigned j = 0; j < bl; j++) a |= (uint16_t)(((b >> j) & 1) << (bl - j - 1));
				*t++ = a;
				t[0x1e] = (uint16_t)((len[i] << 8) | i);
				code = (uint16_t)(code + (1u << (16 - bl)));
			}
}

static uint16_t rnc_value(rnc_t *r, const uint16_t *t)
{
	const uint16_t *end = t + 32;
	uint16_t one, two, v = r->bl;
	do {
		if (t >= end) { r->err = 1; return 0; }
		two = (uint16_t)(*t++ & v);
		one = *t++;
	} while (one != two);
	v = t[0x1e];
	rnc_bits(r, (uint8_t)(v >> 8));
	v &= 0xff;
	if (v >= 2) {
		v--;
		one = rnc_bits(r, (uint8_t)v);
		one |= (uint16_t)(1u << v);
		v = one;
	}
	return v;
}

/* The packed data, n bytes from its "RNC\1" on, is next in 'inf'. Writes the unpacked bytes to
   f; returns how many, or 0 */
static uint32_t rnc_unpack(rnc_t *r, void *inf, uint32_t n, void *f)
{
	r->inf = inf;
	r->ifile = n;
	r->ipos = r->ihave = 0;
	r->err = 0;
	const uint8_t *in = rnc_need(r, 18);
	if (n < 18 || r->err || memcmp(in, "RNC\1", 4)) return 0;
	uint32_t unpack_len = be32(in + 4), pack_len = be32(in + 8);
	uint16_t crc_unpacked = (uint16_t)(in[12] << 8 | in[13]);
	unsigned blocks = in[17];
	if (pack_len + 18 > n + 4) return 0;
	r->ipos = 18;
	r->left = (int32_t)n - 18;
	r->bh = 0; r->bc = 0; r->out = 0; r->crc = 0; r->f = f;
	memset(r->raw, 0, sizeof r->raw); memset(r->pos, 0, sizeof r->pos); memset(r->len, 0, sizeof r->len);
	r->bl = (uint16_t)le16(rnc_need(r, 2));
	rnc_bits(r, 2);
	while (blocks-- && !r->err) {
		rnc_table(r, r->raw);
		rnc_table(r, r->pos);
		rnc_table(r, r->len);
		unsigned counts = rnc_bits(r, 16);
		if (!counts) { r->err = 1; break; }
		do {
			uint32_t len = rnc_value(r, r->raw);
			if (len) {
				if (r->left < (int32_t)len || r->out + len > unpack_len) { r->err = 1; break; }
				r->left -= (int32_t)len;
				while (len && !r->err) {
					uint32_t k = len < 4096 ? len : 4096;
					const uint8_t *src = rnc_need(r, k);
					for (uint32_t i = 0; i < k; i++) rnc_put(r, src[i]);
					r->ipos += k;
					len -= k;
				}
				const uint8_t *src = rnc_need(r, 4);
				uint16_t a = r->left <= 0 ? 0 : r->left == 1 ? src[0] : (uint16_t)le16(src);
				uint16_t b = r->left <= 2 ? 0 : r->left == 3 ? src[2] : (uint16_t)le16(src + 2);
				r->bl &= (uint16_t)((1u << r->bc) - 1);
				r->bl |= (uint16_t)(a << r->bc);
				r->bh = (uint16_t)((a >> (16 - r->bc)) | (b << r->bc));
			}
			if (counts > 1) {
				uint32_t off = (uint32_t)rnc_value(r, r->pos) + 1;
				len = (uint32_t)rnc_value(r, r->len) + 2;
				if (off > r->out || off > 0x8000 || r->out + len > unpack_len) { r->err = 1; break; }
				for (uint32_t i = 0; i < len; i++) rnc_put(r, r->win[(r->out - off) & 0xffff]);
			}
		} while (--counts && !r->err);
	}
	rnc_flush(r, r->out & 0x3fff);
	if (r->err || r->out != unpack_len || r->crc != crc_unpacked) return 0;
	return r->out;
}

#define SKY_HEADER 22                 /* DataFileHeader */
static int sky_in_pack(uint32_t nr) { return nr < 50000 || nr >= 59000; }

static int sky_build(const char *src, uint32_t src_size, const char *dst, void (*progress)(int pct))
{
	pack_head_t h;
	pack_entry_t *e = NULL;
	uint32_t *where = NULL;           /* per entry: offset in sky.dsk | flags in the top bits */
	uint8_t *dnr = NULL, *buf = NULL;
	rnc_t *r = NULL;
	void *f = NULL, *out = NULL;
	char dnr_path[320];
	uint32_t count = 0, total = 0;
	int ok = 0;

	rnc_crc_init();
	memset(&h, 0, sizeof h);
	if (!sky_dnr_path(src, dnr_path, sizeof dnr_path)) return 0;
	plat_stat_t ds = plat_stat(dnr_path);
	if (!ds.exists || ds.size < 12 || ds.size > (1u << 20)) return 0;
	dnr = (uint8_t *)malloc(ds.size);
	f = plat_fopen(dnr_path, "rb");
	if (!dnr || !f || plat_fread(dnr, ds.size, f) != ds.size) goto done;
	plat_fclose(f);
	f = NULL;
	uint32_t n = dnr[0] | dnr[1] << 8 | dnr[2] << 16 | (uint32_t)dnr[3] << 24;
	if (!n || 4 + n * 8 > ds.size) goto done;
	/* the one version whose offsets are in 8-byte units (floppy v0.0331) */
	int shift = (n == 1445 && src_size != 8830435) ? 3 : 4;

	f = plat_fopen(src, "rb");
	if (!f || !fingerprint(f, src_size, &h.src_sum)) goto done;
	e = (pack_entry_t *)malloc(n * sizeof *e);
	where = (uint32_t *)malloc(n * 2 * sizeof *where);
	if (!e || !where) goto done;
	/* what each file will be once unpacked: from the header it starts with */
	for (uint32_t i = 0; i < n; i++) {
		const uint8_t *d = dnr + 4 + i * 8;
		uint32_t nr = le16(d);
		if (!sky_in_pack(nr)) continue;
		uint32_t flags = d[5] | d[6] << 8 | (uint32_t)d[7] << 16, size = flags & 0x3fffff;
		uint32_t off = (d[2] | d[3] << 8 | (uint32_t)d[4] << 16);
		if (off & 0x800000) off = (off & 0x7fffff) << shift;
		if (!size || off + size > src_size) continue;
		uint8_t hd[SKY_HEADER + 4];
		uint32_t got = size < sizeof hd ? size : sizeof hd;
		if (!read_at(f, off, hd, got)) goto done;
		uint32_t unpacked = size, mode = 0;   /* 0: as it is; 1: unpacked alone; 2: header + unpacked */
		if (!(flags & 0x800000) && got == sizeof hd && (le16(hd) & 0x80) && !memcmp(hd + SKY_HEADER, "RNC\1", 4)) {
			unpacked = (le16(hd) & ~0xffu) << 8 | le16(hd + 12);
			mode = (flags & 0x400000) ? 1 : 2;
			if (unpacked < SKY_HEADER) continue;
		}
		e[count].src = nr;
		e[count].size = unpacked;
		where[2 * count] = off;
		where[2 * count + 1] = size | mode << 24;
		count++;
		if (progress && (i & 63) == 0) progress((int)(i * 5 / n));
	}
	if (!count) goto done;
	/* sorted by number, for svm_pack_file() (few are out of order: insertion sort) */
	for (uint32_t i = 1; i < count; i++) {
		pack_entry_t t = e[i];
		uint32_t w0 = where[2 * i], w1 = where[2 * i + 1], j = i;
		for (; j && e[j - 1].src > t.src; j--) { e[j] = e[j - 1]; where[2 * j] = where[2 * j - 2]; where[2 * j + 1] = where[2 * j - 1]; }
		e[j] = t; where[2 * j] = w0; where[2 * j + 1] = w1;
	}
	h.magic = PACK_MAGIC;
	h.version = PACK_VERSION;
	h.src_size = src_size;
	h.count = count;
	h.reserved = PACK_SKY;
	h.data_off = sizeof h + count * sizeof *e;
	total = h.data_off;
	for (uint32_t i = 0; i < count; i++) {
		e[i].pack = total;
		total += (e[i].size + 3) & ~3u;
	}
	h.total = total;

	r = (rnc_t *)calloc(1, sizeof *r);
	if (r) { r->win = (uint8_t *)malloc(65536); r->ibuf = (uint8_t *)malloc(RNC_IN); }
	buf = (uint8_t *)malloc(COPY_CHUNK);
	out = plat_fopen(dst, "wb");
	if (!r || !r->win || !r->ibuf || !buf || !out) goto done;
	pack_head_t blank;
	memset(&blank, 0, sizeof blank);
	if (plat_fwrite(&blank, sizeof blank, out) != sizeof blank ||
	    plat_fwrite(e, count * sizeof *e, out) != count * sizeof *e) goto done;
	uint32_t done_bytes = 0, step = total / 95 + 1, next = 0;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t off = where[2 * i], size = where[2 * i + 1] & 0xffffff, mode = where[2 * i + 1] >> 24;
		if (!plat_fseek(f, (int32_t)off, 0)) goto done;
		if (mode) {
			uint32_t want = e[i].size;
			if (plat_fread(buf, SKY_HEADER, f) != SKY_HEADER) goto done;
			if (mode == 2) {
				if (plat_fwrite(buf, SKY_HEADER, out) != SKY_HEADER) goto done;
				want -= SKY_HEADER;
			}
			if (rnc_unpack(r, f, size - SKY_HEADER, out) != want) goto done;
		} else {
			for (uint32_t left = size; left;) {
				uint32_t k = left < COPY_CHUNK ? left : COPY_CHUNK;
				if (plat_fread(buf, k, f) != k || plat_fwrite(buf, k, out) != k) goto done;
				left -= k;
			}
		}
		uint32_t pad = ((e[i].size + 3) & ~3u) - e[i].size;
		static const uint8_t zero[4];
		if (pad && plat_fwrite(zero, pad, out) != pad) goto done;
		done_bytes += e[i].size;
		if (progress && done_bytes >= next) {
			progress(5 + (int)((uint64_t)done_bytes * 95 / total));
			next = done_bytes + step;
		}
	}
	if (!plat_fseek(out, 0, 0) || plat_fwrite(&h, sizeof h, out) != sizeof h) goto done;
	ok = 1;
done:
	if (out) plat_fclose(out);
	if (f) plat_fclose(f);
	if (r) { free(r->win); free(r->ibuf); }
	free(r); free(buf); free(dnr); free(e); free(where);
	return ok;
}

const pack_entry_t *svm_pack_file(const pack_head_t *h, uint32_t nr)
{
	const pack_entry_t *e = (const pack_entry_t *)(h + 1);
	uint32_t lo = 0, hi = h->count;
	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;
		if (e[mid].src < nr) lo = mid + 1;
		else hi = mid;
	}
	return lo < h->count && e[lo].src == nr ? &e[lo] : NULL;
}
