/*
 * Decodes a file with the core's audio decoders (src/gw_codecs.h) to raw 16-bit PCM, to
 * compare with a reference decoder (ffmpeg):
 *
 *   gcc -O2 -Isrc tools/host/codec_test.c src/codecs/*.c -lm -o codec_test
 *   ./codec_test ogg|mp3|flac in out.raw [seek_frame]
 *   ffmpeg -i in -f s16le ref.raw
 *
 * With seek_frame it seeks there first (and prints the length in frames).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gw_codecs.h"

static uint32_t rd(void *f, void *b, uint32_t n) { return (uint32_t)fread(b, 1, n, (FILE *)f); }
static int sk(void *f, uint32_t p) { return fseek((FILE *)f, (long)p, SEEK_SET) == 0; }

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s ogg|mp3|flac in out.raw [seek_frame]\n", argv[0]); return 2; }
	int kind = !strcmp(argv[1], "ogg") ? GWC_VORBIS : !strcmp(argv[1], "mp3") ? GWC_MP3 : GWC_FLAC;
	FILE *f = fopen(argv[2], "rb"), *o = fopen(argv[3], "wb");
	if (!f || !o) { perror("open"); return 1; }
	fseek(f, 0, SEEK_END);
	gwc_io io = { f, rd, sk, (uint32_t)ftell(f) };
	fseek(f, 0, SEEK_SET);
	gwc *c = gwc_open(kind, &io);
	if (!c) { fprintf(stderr, "not opened\n"); return 1; }
	printf("rate %d channels %d length %u\n", gwc_rate(c), gwc_channels(c), gwc_length(c));
	if (argc > 4 && !gwc_seek(c, (uint32_t)atoi(argv[4]))) { fprintf(stderr, "seek failed\n"); return 1; }
	int16_t buf[4096];
	int n, total = 0;
	while ((n = gwc_read(c, buf, 4096 / gwc_channels(c))) > 0) {
		fwrite(buf, 2 * gwc_channels(c), n, o);
		total += n;
	}
	printf("%d frames\n", total);
	gwc_close(c);
	fclose(o);
	return 0;
}
