/*
 * src/gw_codecs.h: the decoders behind one interface. Memory comes from the heap
 * (src/heap.c) and lasts as long as the clip: a speech clip opens a decoder, plays, closes it.
 */
#include "gwc_priv.h"

gwc *gwc_open(int kind, const gwc_io *io)
{
	gwc *c = (gwc *)calloc(1, sizeof *c);
	if (!c) return NULL;
	c->kind = kind;
	c->r.io = *io;
	int ok = kind == GWC_VORBIS ? gwc_vorbis_open(c) : kind == GWC_MP3 ? gwc_mp3_open(c) :
	         kind == GWC_FLAC ? gwc_flac_open(c) : 0;
	if (!ok || c->channels < 1 || c->channels > 2 || c->rate < 1000) {
		gwc_close(c);
		return NULL;
	}
	return c;
}

int gwc_rate(const gwc *c) { return c->rate; }
int gwc_channels(const gwc *c) { return c->channels; }

int gwc_read(gwc *c, int16_t *out, int frames)
{
	return c->kind == GWC_VORBIS ? gwc_vorbis_read(c, out, frames) :
	       c->kind == GWC_MP3 ? gwc_mp3_read(c, out, frames) : gwc_flac_read(c, out, frames);
}

int gwc_seek(gwc *c, uint32_t frame)
{
	return c->kind == GWC_VORBIS ? gwc_vorbis_seek(c, frame) :
	       c->kind == GWC_MP3 ? gwc_mp3_seek(c, frame) : gwc_flac_seek(c, frame);
}

uint32_t gwc_length(gwc *c)
{
	return c->kind == GWC_VORBIS ? gwc_vorbis_length(c) :
	       c->kind == GWC_MP3 ? gwc_mp3_length(c) : gwc_flac_length(c);
}

void gwc_close(gwc *c)
{
	if (!c) return;
	if (c->dec) {
		if (c->kind == GWC_VORBIS) gwc_vorbis_close(c);
		else if (c->kind == GWC_MP3) gwc_mp3_close(c);
		else gwc_flac_close(c);
	}
	free(c);
}
