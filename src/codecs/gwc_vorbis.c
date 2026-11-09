/*
 * Ogg Vorbis with stb_vorbis (public domain; the File Manager's copy, src/third_party).
 * Its setup (codebooks) comes from malloc; while decoding it takes a few KB of stack (alloca).
 */
#include "gwc_priv.h"

#define FILE rd_t
#define fgetc(o)            rd_getc(o)
#define fread(d, s, n, o)   ((s) ? rd_read(o, d, (uint32_t)((s) * (n))) / (s) : 0)
#define fseek(o, off, w)    rd_seek(o, off, w)
#define ftell(o)            ((long)(o)->pos)
#define fclose(o)           ((void)(o))
#define fopen(n, m)         ((rd_t *)0)

#define STB_VORBIS_MAX_CHANNELS          2
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_FAST_HUFFMAN_LENGTH   8        /* 10: +2 KB per codebook */
#define STB_VORBIS_DIVIDES_IN_CODEBOOK            /* no expanded lookup tables */
#ifndef NDEBUG
#define NDEBUG
#endif
#include "../third_party/stb_vorbis/stb_vorbis.c"

int gwc_vorbis_open(gwc *c)
{
	int err = 0;
	stb_vorbis *v = stb_vorbis_open_file_section(&c->r, 0, &err, NULL, c->r.io.size);
	if (!v) return 0;
	stb_vorbis_info vi = stb_vorbis_get_info(v);
	c->rate = (int)vi.sample_rate;
	c->channels = vi.channels;
	c->dec = v;
	return 1;
}

int gwc_vorbis_read(gwc *c, int16_t *out, int frames)
{
	return stb_vorbis_get_samples_short_interleaved((stb_vorbis *)c->dec, c->channels, out, frames * c->channels);
}

int gwc_vorbis_seek(gwc *c, uint32_t frame) { return stb_vorbis_seek((stb_vorbis *)c->dec, frame); }

uint32_t gwc_vorbis_length(gwc *c)
{
	unsigned int n = stb_vorbis_stream_length_in_samples((stb_vorbis *)c->dec);
	return n == SAMPLE_unknown ? 0 : n;
}

void gwc_vorbis_close(gwc *c) { stb_vorbis_close((stb_vorbis *)c->dec); }
