/*
 * Compressed audio for ScummVM on the console: the speech of talkie versions (.sog / .so3 /
 * .sof, one compressed clip per line) and CD music tracks (trackN.ogg / .mp3 / .flac).
 *
 * ScummVM's own decoders want libvorbis/Tremor, libmad and libFLAC; these are the small
 * single-file decoders the other Game & Watch cores use instead (stb_vorbis, minimp3,
 * dr_flac, all public domain), behind one interface. src/gw_audio_decoders.cpp turns it into
 * ScummVM's makeVorbisStream() / makeMP3Stream() / makeFLACStream().
 */
#ifndef GW_CODECS_H
#define GW_CODECS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	void *ctx;
	uint32_t (*read)(void *ctx, void *buf, uint32_t n);   /* bytes read, 0 at the end */
	int (*seek)(void *ctx, uint32_t pos);                  /* from the start; 0 on failure */
	uint32_t size;                                         /* bytes in the stream */
} gwc_io;

enum { GWC_VORBIS, GWC_MP3, GWC_FLAC };

typedef struct gwc gwc;

gwc     *gwc_open(int kind, const gwc_io *io);          /* NULL: not that format, or no memory */
int      gwc_rate(const gwc *c);
int      gwc_channels(const gwc *c);                    /* 1 or 2 */
int      gwc_read(gwc *c, int16_t *out, int frames);    /* interleaved; fewer than asked at the end */
int      gwc_seek(gwc *c, uint32_t frame);              /* 1 on success */
uint32_t gwc_length(gwc *c);                            /* in frames, 0 if unknown */
void     gwc_close(gwc *c);

#ifdef __cplusplus
}
#endif

#endif
