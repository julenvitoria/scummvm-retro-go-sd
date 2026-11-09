/* FLAC with dr_flac (public domain / MIT-0, src/third_party). It allocates with malloc. */
#include "gwc_priv.h"

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#include "../third_party/dr_flac/dr_flac.h"

static size_t flac_read(void *u, void *buf, size_t n) { return rd_read((rd_t *)u, buf, (uint32_t)n); }
static drflac_bool32 flac_seek(void *u, int off, drflac_seek_origin o)
{
	return rd_seek((rd_t *)u, off, o == drflac_seek_origin_start ? SEEK_SET : SEEK_CUR) == 0;
}

int gwc_flac_open(gwc *c)
{
	drflac *f = drflac_open(flac_read, flac_seek, &c->r, NULL);
	if (!f) return 0;
	c->rate = (int)f->sampleRate;
	c->channels = f->channels;
	c->dec = f;
	return 1;
}

int gwc_flac_read(gwc *c, int16_t *out, int frames)
{
	return (int)drflac_read_pcm_frames_s16((drflac *)c->dec, (drflac_uint64)frames, out);
}

int gwc_flac_seek(gwc *c, uint32_t frame) { return drflac_seek_to_pcm_frame((drflac *)c->dec, frame); }
uint32_t gwc_flac_length(gwc *c) { return (uint32_t)((drflac *)c->dec)->totalPCMFrameCount; }
void gwc_flac_close(gwc *c) { drflac_close((drflac *)c->dec); }
