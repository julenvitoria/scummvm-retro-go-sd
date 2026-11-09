/*
 * Shared by the decoders behind src/gw_codecs.h: one buffered reader and the decoder object.
 * Each decoder is a file of its own (stb_vorbis and minimp3 have static functions of the
 * same name).
 */
#ifndef GWC_PRIV_H
#define GWC_PRIV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gw_codecs.h"

#define RD_BUF 4096

/* stb_vorbis reads a byte at a time and the clips of a speech file come from the SD card */
typedef struct {
	gwc_io io;
	uint32_t pos;                 /* logical position */
	uint32_t bstart, blen;        /* what buf holds */
	uint8_t buf[RD_BUF];
} rd_t;

static inline int rd_fill(rd_t *r)
{
	if (r->pos >= r->io.size) return 0;
	uint32_t n = r->io.size - r->pos < RD_BUF ? r->io.size - r->pos : RD_BUF;
	if (!r->io.seek(r->io.ctx, r->pos)) return 0;
	uint32_t got = r->io.read(r->io.ctx, r->buf, n);
	r->bstart = r->pos;
	r->blen = got;
	return got > 0;
}

static inline int rd_has(const rd_t *r) { return r->pos >= r->bstart && r->pos < r->bstart + r->blen; }

static inline int rd_getc(rd_t *r)
{
	if (!rd_has(r) && !rd_fill(r)) return EOF;
	return r->buf[r->pos++ - r->bstart];
}

static inline uint32_t rd_read(rd_t *r, void *dst, uint32_t want)
{
	uint8_t *d = (uint8_t *)dst;
	uint32_t done = 0;
	while (done < want) {
		if (!rd_has(r) && !rd_fill(r)) break;
		uint32_t avail = r->bstart + r->blen - r->pos, n = want - done < avail ? want - done : avail;
		memcpy(d + done, r->buf + (r->pos - r->bstart), n);
		r->pos += n;
		done += n;
	}
	return done;
}

static inline int rd_seek(rd_t *r, long off, int whence)
{
	long long p = whence == SEEK_END ? (long long)r->io.size + off : whence == SEEK_CUR ? (long long)r->pos + off : off;
	if (p < 0) return -1;
	if (p > r->io.size) { r->pos = r->io.size; return -1; }
	r->pos = (uint32_t)p;
	return 0;
}

struct gwc {
	int kind, rate, channels;
	void *dec;                    /* the decoder's own state */
	rd_t r;
};

/* each returns 0 on failure; open fills rate and channels */
int      gwc_vorbis_open(gwc *c);
int      gwc_vorbis_read(gwc *c, int16_t *out, int frames);
int      gwc_vorbis_seek(gwc *c, uint32_t frame);
uint32_t gwc_vorbis_length(gwc *c);
void     gwc_vorbis_close(gwc *c);

int      gwc_mp3_open(gwc *c);
int      gwc_mp3_read(gwc *c, int16_t *out, int frames);
int      gwc_mp3_seek(gwc *c, uint32_t frame);
uint32_t gwc_mp3_length(gwc *c);
void     gwc_mp3_close(gwc *c);

int      gwc_flac_open(gwc *c);
int      gwc_flac_read(gwc *c, int16_t *out, int frames);
int      gwc_flac_seek(gwc *c, uint32_t frame);
uint32_t gwc_flac_length(gwc *c);
void     gwc_flac_close(gwc *c);

#endif
