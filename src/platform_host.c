/*
 * platform.h on a PC, for testing the backend without the console.
 *
 *   ./scummvm_host <game folder> [scummvm arguments...]
 *
 *   GW_SHOTS=dir   save the screen as dir/fNNNNN.ppm (NNNNN = time in 1/10 s)
 *   GW_EVERY=ms    ...at most every ms milliseconds (default 1000)
 *   GW_INPUT="t:buttons:ms;..."  hold buttons (letters u d l r a b g t s p) at t ms for ms
 *   GW_QUIT=ms     exit after ms milliseconds
 *   GW_SAVES=dir   saves and scummvm.ini (default ./saves)
 *
 * plat_map_file() reads the whole file into memory once, standing in for the flash copy.
 */
#define _GNU_SOURCE
#include "platform.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char *g_game, *g_saves;
static struct timeval g_t0;
static uint8_t g_pal[768];

void plat_log(const char *msg) { fputs(msg, stdout); fflush(stdout); }
void plat_fatal(void) { exit(1); }

void plat_quit(void) { exit(0); }
const char *plat_game_path(void) { return g_game; }
const char *plat_save_path(void) { return g_saves; }

uint32_t plat_millis(void)
{
	struct timeval t;
	gettimeofday(&t, 0);
	uint32_t ms = (uint32_t)((t.tv_sec - g_t0.tv_sec) * 1000 + (t.tv_usec - g_t0.tv_usec) / 1000);
	const char *q = getenv("GW_QUIT");
	if (q && ms > (uint32_t)atoi(q)) exit(0);
	return ms;
}
/* the console's heap.c: the PC never runs low */
uint32_t heap_free_bytes(void) __attribute__((weak));
uint32_t heap_free_bytes(void) { return 64u << 20; }
int heap_screen_hint;   /* shared with tools/host/gwheap.c when it is preloaded */
int heap_can_alloc(uint32_t n) __attribute__((weak));
int heap_can_alloc(uint32_t n) { (void)n; return 1; }

/* ---- sound: GW_WAV=file.wav records what the core would play (mono, 22050 Hz), in real time */
#define HOST_RATE 22050
static void (*g_pump)(void);
static FILE *g_wav;
static uint32_t g_wav_samples;

void plat_set_pump(void (*fn)(void)) { g_pump = fn; }
uint32_t plat_audio_rate(void) { return getenv("GW_WAV") ? HOST_RATE : 0; }
int plat_audio_free(void)
{
	/* what the console's DMA would have consumed by now, plus ~0.1 s of buffer */
	uint64_t due = (uint64_t)plat_millis() * HOST_RATE / 1000 + HOST_RATE / 10;
	return due > g_wav_samples ? (int)(due - g_wav_samples) : 0;
}
static void wav_header(void)
{
	uint32_t data = g_wav_samples * 2, v;
	fseek(g_wav, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, g_wav); v = 36 + data; fwrite(&v, 4, 1, g_wav);
	fwrite("WAVEfmt ", 1, 8, g_wav); v = 16; fwrite(&v, 4, 1, g_wav);
	uint16_t h[2] = { 1, 1 }; fwrite(h, 2, 2, g_wav);
	v = HOST_RATE; fwrite(&v, 4, 1, g_wav); v = HOST_RATE * 2; fwrite(&v, 4, 1, g_wav);
	uint16_t h2[2] = { 2, 16 }; fwrite(h2, 2, 2, g_wav);
	fwrite("data", 1, 4, g_wav); fwrite(&data, 4, 1, g_wav);
	fseek(g_wav, 0, SEEK_END);
}
static void wav_close(void) { if (g_wav) { wav_header(); fclose(g_wav); g_wav = 0; } }
void plat_audio_write(const int16_t *smp, int n)
{
	if (!g_wav && getenv("GW_WAV")) {
		g_wav = fopen(getenv("GW_WAV"), "wb+");
		if (g_wav) { wav_header(); atexit(wav_close); }
	}
	if (g_wav) fwrite(smp, 2, n, g_wav);
	g_wav_samples += n;
}

void plat_delay(uint32_t ms)
{
	uint32_t end = plat_millis() + ms;
	do {
		if (g_pump) g_pump();
		uint32_t now = plat_millis();
		if (now >= end) break;
		usleep((end - now > 5 ? 5 : end - now) * 1000);
	} while (1);
}
void plat_date(int *year, int *mon, int *mday, int *hour, int *min, int *sec, int *wday)
{
	time_t now = time(0);
	struct tm t = *localtime(&now);
	*year = t.tm_year; *mon = t.tm_mon; *mday = t.tm_mday;
	*hour = t.tm_hour; *min = t.tm_min; *sec = t.tm_sec; *wday = t.tm_wday;
}

void plat_set_palette(const uint8_t *rgb, int start, int count) { memcpy(g_pal + start * 3, rgb, count * 3); }

void plat_present(const uint8_t *pixels, int pitch, int w, int h)
{
	static uint32_t next;
	if (g_pump) g_pump();
	const char *dir = getenv("GW_SHOTS");
	if (!dir) return;
	uint32_t now = plat_millis();
	if (now < next) return;
	next = now + (getenv("GW_EVERY") ? atoi(getenv("GW_EVERY")) : 1000);
	char path[512];
	snprintf(path, sizeof path, "%s/f%05u.ppm", dir, now / 100);
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) fwrite(g_pal + pixels[y * pitch + x] * 3, 1, 3, f);
	fclose(f);
}

uint32_t plat_buttons(void)
{
	const char *p = getenv("GW_INPUT");
	uint32_t now = plat_millis(), held = 0;
	while (p && *p) {
		unsigned t, len; char keys[16];
		if (sscanf(p, "%u:%15[a-z]:%u", &t, keys, &len) != 3) break;
		if (now >= t && now < t + len)
			for (char *k = keys; *k; k++)
				held |= *k == 'u' ? PLAT_UP : *k == 'd' ? PLAT_DOWN : *k == 'l' ? PLAT_LEFT : *k == 'r' ? PLAT_RIGHT :
				        *k == 'a' ? PLAT_A : *k == 'b' ? PLAT_B : *k == 'g' ? PLAT_GAME : *k == 't' ? PLAT_TIME :
				        *k == 's' ? PLAT_SELECT : *k == 'p' ? PLAT_START : 0;
		p = strchr(p, ';');
		if (p) p++;
	}
	return held;
}

plat_stat_t plat_stat(const char *path)
{
	struct stat s;
	plat_stat_t r = { false, false, 0 };
	if (stat(path, &s) == 0) { r.exists = true; r.is_dir = S_ISDIR(s.st_mode); r.size = (uint32_t)s.st_size; }
	return r;
}

bool plat_list_dir(const char *path, void (*cb)(const char *, bool, uint32_t, void *), void *arg)
{
	DIR *d = opendir(path);
	if (!d) return false;
	struct dirent *e;
	char full[1024];
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		snprintf(full, sizeof full, "%s/%s", path, e->d_name);
		plat_stat_t s = plat_stat(full);
		cb(e->d_name, s.is_dir, s.size, arg);
	}
	closedir(d);
	return true;
}

const uint8_t *plat_map_file(const char *path, uint32_t *size)
{
	static struct { char path[512]; uint8_t *data; uint32_t size; } map[64];
	if (!g_game || strncmp(path, g_game, strlen(g_game)) != 0) return NULL;   /* only the game's own files */
	for (int i = 0; i < 64; i++) {
		if (map[i].data && !strcmp(map[i].path, path)) { *size = map[i].size; return map[i].data; }
		if (!map[i].data) {
			FILE *f = fopen(path, "rb");
			if (!f) return NULL;
			fseek(f, 0, SEEK_END);
			long n = ftell(f);
			fseek(f, 0, SEEK_SET);
			/* outside the heap, like the console's external flash (tools/host/gwheap.c caps the heap) */
			map[i].data = mmap(0, n ? n : 1, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (map[i].data == MAP_FAILED || fread(map[i].data, 1, n, f) != (size_t)n) { fclose(f); map[i].data = NULL; return NULL; }
			fclose(f);
			snprintf(map[i].path, sizeof map[i].path, "%s", path);
			map[i].size = (uint32_t)n;
			*size = map[i].size;
			return map[i].data;
		}
	}
	return NULL;
}

bool plat_mkdir(const char *path) { return mkdir(path, 0777) == 0; }
void *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }
uint32_t plat_fread(void *buf, uint32_t n, void *f) { return (uint32_t)fread(buf, 1, n, f); }
uint32_t plat_fwrite(const void *buf, uint32_t n, void *f) { return (uint32_t)fwrite(buf, 1, n, f); }
bool plat_fseek(void *f, int32_t off, int whence) { return fseek(f, off, whence) == 0; }
int32_t plat_ftell(void *f) { return (int32_t)ftell(f); }
void plat_fclose(void *f) { fclose(f); }

int gw_scummvm_main(int argc, const char *const argv[]);

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s <game folder> [scummvm args]\n", argv[0]); return 1; }
	gettimeofday(&g_t0, 0);
	g_game = argv[1];
	g_saves = getenv("GW_SAVES") ? getenv("GW_SAVES") : "saves";
	mkdir(g_saves, 0777);
	const char *args[32];
	int n = 0;
	args[n++] = "scummvm";
	args[n++] = "-p";
	args[n++] = g_game;
	for (int i = 2; i < argc && n < 30; i++) args[n++] = argv[i];
	if (argc == 2) args[n++] = "--auto-detect";   /* as on the console */
	args[n] = NULL;
	return gw_scummvm_main(n, args);
}
