/*
 * MP3 with minimp3 (CC0; the File Manager's copy, src/third_party, whose 16 KB scratch can
 * live outside the stack: here it is part of the decoder, from the heap).
 */
#include "gwc_priv.h"

static void *g_scratch;                   /* the decoding clip's, set before each frame */
#define MINIMP3_SCRATCH_PTR g_scratch
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3/minimp3.h"

#define MP3_IN (8 * 1024)

typedef struct {
	mp3dec_t dec;
	mp3dec_scratch_t scratch;
	uint32_t data_start;                  /* the first frame with samples (after an ID3v2 tag and
	                                         a Xing/LAME frame, which counting alone would not spot) */
	uint32_t frame_start;                 /* where the last frame returned began in the file */
	uint32_t in_len, in_pos;              /* bytes in in[] and where the next frame starts */
	uint8_t in[MP3_IN];
	int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	int pcm_frames, pcm_pos;              /* decoded frames waiting in pcm[] */
	uint32_t played;                      /* frame of the stream the next one out is */
	uint32_t length;                      /* frames, once counted (1: counted, unknown) */
	int eof;
} mp3_t;

/* move what is left to the front of in[] and read on */
static void refill(rd_t *r, mp3_t *m)
{
	if (m->in_pos) {
		memmove(m->in, m->in + m->in_pos, m->in_len - m->in_pos);
		m->in_len -= m->in_pos;
		m->in_pos = 0;
	}
	if (m->in_len < MP3_IN && !m->eof) {
		uint32_t got = rd_read(r, m->in + m->in_len, MP3_IN - m->in_len);
		if (!got) m->eof = 1;
		m->in_len += got;
	}
}

/* the next frame: decoded into pcm[] or (decode == 0) only counted; its samples, 0 at the end */
static int next_frame(rd_t *r, mp3_t *m, int decode, int *rate, int *channels)
{
	for (;;) {
		if (m->in_len - m->in_pos < 2048) refill(r, m);
		if (m->in_len == m->in_pos) return 0;
		mp3dec_frame_info_t info;
		uint32_t start = r->pos - (m->in_len - m->in_pos);
		g_scratch = &m->scratch;
		int n = mp3dec_decode_frame(&m->dec, m->in + m->in_pos, (int)(m->in_len - m->in_pos), decode ? m->pcm : NULL, &info);
		if (!info.frame_bytes) {              /* no frame in what is buffered */
			m->in_pos = m->in_len;
			if (m->eof) return 0;
			continue;
		}
		m->in_pos += info.frame_bytes;
		if (n <= 0 && decode && info.layer) {
			/* a frame whose bit reservoir is in frames not decoded (right after a seek): its
			   time still counts, as silence, or the position drifts by a frame */
			n = info.hz >= 32000 ? 1152 : 576;          /* layer III: MPEG-1 / MPEG-2 and 2.5 */
			memset(m->pcm, 0, (size_t)n * info.channels * sizeof(int16_t));
		}
		if (n <= 0) continue;
		m->frame_start = start;
		if (rate) *rate = info.hz;
		if (channels) *channels = info.channels;
		m->pcm_frames = decode ? n : 0;
		m->pcm_pos = 0;
		return n;
	}
}

static void restart(rd_t *r, mp3_t *m)
{
	mp3dec_init(&m->dec);
	r->pos = m->data_start;
	m->in_len = m->in_pos = 0;
	m->pcm_frames = m->pcm_pos = 0;
	m->played = 0;
	m->eof = 0;
}

int gwc_mp3_open(gwc *c)
{
	mp3_t *m = (mp3_t *)calloc(1, sizeof *m);
	if (!m) return 0;
	c->dec = m;
	uint8_t id3[10];
	if (rd_read(&c->r, id3, 10) == 10 && !memcmp(id3, "ID3", 3))
		m->data_start = 10 + ((uint32_t)(id3[6] & 0x7f) << 21 | (uint32_t)(id3[7] & 0x7f) << 14 |
		                      (uint32_t)(id3[8] & 0x7f) << 7 | (id3[9] & 0x7f)) + (id3[5] & 0x10 ? 10 : 0);
	restart(&c->r, m);
	if (next_frame(&c->r, m, 1, &c->rate, &c->channels) <= 0) return 0;   /* the first frame says what it is */
	m->data_start = m->frame_start;
	return 1;
}

int gwc_mp3_read(gwc *c, int16_t *out, int frames)
{
	mp3_t *m = (mp3_t *)c->dec;
	int done = 0;
	while (done < frames) {
		if (m->pcm_pos >= m->pcm_frames) {
			int ch = c->channels;
			if (!next_frame(&c->r, m, 1, NULL, &ch)) break;
			if (ch == 1 && c->channels == 2)         /* a mono frame in a stereo stream */
				for (int i = m->pcm_frames - 1; i >= 0; i--) m->pcm[2 * i] = m->pcm[2 * i + 1] = m->pcm[i];
			else if (ch == 2 && c->channels == 1)    /* and the reverse */
				for (int i = 0; i < m->pcm_frames; i++) m->pcm[i] = (int16_t)((m->pcm[2 * i] + m->pcm[2 * i + 1]) / 2);
		}
		int n = m->pcm_frames - m->pcm_pos;
		if (n > frames - done) n = frames - done;
		memcpy(out + done * c->channels, m->pcm + m->pcm_pos * c->channels, (size_t)n * c->channels * sizeof(int16_t));
		m->pcm_pos += n;
		done += n;
	}
	m->played += (uint32_t)done;
	return done;
}

/* from the start: frames well before the target are only counted (a frame has at most 1152
   samples), the one with it is decoded */
int gwc_mp3_seek(gwc *c, uint32_t frame)
{
	mp3_t *m = (mp3_t *)c->dec;
	restart(&c->r, m);
	uint32_t at = 0;
	for (;;) {
		int decode = frame - at < 1152;
		int n = next_frame(&c->r, m, decode, NULL, NULL);
		if (n <= 0) return 0;
		if (at + (uint32_t)n > frame) {
			m->pcm_pos = (int)(frame - at);
			m->played = frame;
			return 1;
		}
		at += (uint32_t)n;
	}
}

/* counted once from the frame headers, then back to where it was */
uint32_t gwc_mp3_length(gwc *c)
{
	mp3_t *m = (mp3_t *)c->dec;
	if (!m->length) {
		uint32_t played = m->played, total = 0;
		int n;
		restart(&c->r, m);
		while ((n = next_frame(&c->r, m, 0, NULL, NULL)) > 0) total += (uint32_t)n;
		m->length = total ? total : 1;
		gwc_mp3_seek(c, played);
	}
	return m->length > 1 ? m->length : 0;
}

void gwc_mp3_close(gwc *c) { free(c->dec); }
