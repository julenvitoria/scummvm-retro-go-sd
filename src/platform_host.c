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
 *   GW_WDOG=ms     the console's watchdog, roughly: a backtrace on stderr whenever the program
 *                  computes for ms of CPU time without asking the time or reading a file (the
 *                  console resets after ~0.5 s of that; the PC is some 30 times faster)
 *
 * plat_map_file() reads the whole file into memory once, standing in for the flash copy.
 */
#define _GNU_SOURCE
#include "platform.h"
#include "pack.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <execinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char *g_game, *g_saves;
static struct timeval g_t0;
static uint8_t g_pal[768];

void plat_log(const char *msg)
{
	fputs(msg, stdout);
	fflush(stdout);
	if (!strncmp(msg, "heap: out of memory", 19)) {      /* host-gwheap: who asked */
		void *bt[16];
		backtrace_symbols_fd(bt, backtrace(bt, 16), 1);
	}
}
void plat_fatal(void) { exit(1); }
void plat_redraw(void) {}

void plat_quit(void) { exit(0); }
const char *plat_game_path(void) { return g_game; }
const char *plat_save_path(void) { return g_saves; }

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
static void wdog_bark(int sig)
{
	void *bt[24];
	(void)sig;
	(void)!write(2, "GW_WDOG: no refresh; backtrace:\n", 32);
	backtrace_symbols_fd(bt, backtrace(bt, 24), 2);
}
static void wdog_refresh(void)
{
	static int ms = -1;
	if (ms < 0) {
		ms = getenv("GW_WDOG") ? atoi(getenv("GW_WDOG")) : 0;
		if (ms) signal(SIGVTALRM, wdog_bark);
	}
	if (!ms) return;
	struct itimerval it = { { 0, 0 }, { ms / 1000, (ms % 1000) * 1000 } };
	setitimer(ITIMER_VIRTUAL, &it, 0);
}

uint32_t plat_millis(void)
{
	wdog_refresh();
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
int heap_screen_hint __attribute__((weak));   /* src/heap.c defines it in host-gwheap */
int heap_screen_spill __attribute__((weak));
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

/* the Retro-Go menu's save and load (src/gw_system.cpp), as the console's firmware calls them:
   GW_RGSAVE=<ms> saves to <saves>/rg.sav at that time, GW_RGLOAD=<ms> loads it (0: at start),
   GW_RGLOAD_EVERY=<ms> loads it again and again */
int gw_rg_save(const char *path);
int gw_rg_load(const char *path);
int gw_rg_load_at_start(void);
static const char *rg_path(void) { static char p[600]; snprintf(p, sizeof p, "%s/rg.sav", g_saves); return p; }

uint32_t plat_buttons(void)
{
	const char *p = getenv("GW_INPUT");
	uint32_t now = plat_millis(), held = 0;
	static int saved, loaded;
	if (!saved && getenv("GW_RGSAVE") && now >= (uint32_t)atoi(getenv("GW_RGSAVE"))) {
		saved = 1;
		fprintf(stderr, "rg save at %u: %d\n", now, gw_rg_save(rg_path()));
	}
	if (!loaded && getenv("GW_RGLOAD") && atoi(getenv("GW_RGLOAD")) && now >= (uint32_t)atoi(getenv("GW_RGLOAD"))) {
		loaded = 1;
		fprintf(stderr, "rg load at %u: %d\n", now, gw_rg_load(rg_path()));
	}
	/* GW_RGLOAD_EVERY=<ms>: ...and again every so often, with the heap in use before each
	   (a leak, or a heap that fragments, shows as a load that fails after a few) */
	static uint32_t again;
	if (loaded && getenv("GW_RGLOAD_EVERY")) {
		extern uint32_t heap_used(void) __attribute__((weak));
		if (!again) again = now + atoi(getenv("GW_RGLOAD_EVERY"));
		if (now >= again) {
			again = now + atoi(getenv("GW_RGLOAD_EVERY"));
			fprintf(stderr, "rg load at %u (heap %u KB): %d\n", now, heap_used ? heap_used() / 1024 : 0, gw_rg_load(rg_path()));
		}
	}
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

/* FatFs finds names in any case, Linux does not: a path that is not there as written is
   looked up one component at a time, ignoring case (the engine asks for "ft.la0" when the
   file is FT.LA0, and the core no longer lists whole folders to find out: GW_FS_PROBE) */
static const char *ci(const char *path)
{
	static char out[1024];
	struct stat st;
	if (!path || !*path || stat(path, &st) == 0 || strlen(path) >= sizeof out) return path;
	extern int heap_host_bypass __attribute__((weak));
	size_t o = 0;
	const char *p = path;
	if (*p == '/') { out[o++] = '/'; p++; }
	out[o] = 0;
	while (*p) {
		const char *e = strchr(p, '/');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char comp[256];
		if (n >= sizeof comp) return path;
		memcpy(comp, p, n); comp[n] = 0;
		char trial[1024];
		snprintf(trial, sizeof trial, "%s%s", out, comp);
		if (stat(trial, &st) != 0) {
			if (&heap_host_bypass) heap_host_bypass = 1;
			DIR *d = opendir(o ? out : ".");
			if (&heap_host_bypass) heap_host_bypass = 0;
			if (!d) return path;
			struct dirent *de;
			int found = 0;
			while ((de = readdir(d)))
				if (!strcasecmp(de->d_name, comp)) { snprintf(comp, sizeof comp, "%s", de->d_name); found = 1; break; }
			closedir(d);
			if (!found) return path;
		}
		o += snprintf(out + o, sizeof out - o, "%s%s", comp, e ? "/" : "");
		p += n + (e ? 1 : 0);
	}
	return out;
}

plat_stat_t plat_stat(const char *path)
{
	path = ci(path);
	struct stat s;
	plat_stat_t r = { false, false, 0 };
	if (stat(path, &s) == 0) { r.exists = true; r.is_dir = S_ISDIR(s.st_mode); r.size = (uint32_t)s.st_size; }
	return r;
}

bool plat_list_dir(const char *path, void (*cb)(const char *, bool, uint32_t, void *), void *arg)
{
	extern int heap_host_bypass __attribute__((weak));
	if (&heap_host_bypass) heap_host_bypass = 1;
	DIR *d = opendir(path);
	if (&heap_host_bypass) heap_host_bypass = 0;
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

const uint8_t *plat_map_file(const char *path, uint32_t *size, uint8_t *key)
{
	static struct { char path[512]; uint8_t *data; uint32_t size; uint8_t key; } map[64];
	char own[1024];
	snprintf(own, sizeof own, "%s", ci(path));
	path = own;
	/* as on the console: bigger than its flash cache takes stays on the "card" (FT.LA1: its
	   resource pack is mapped instead, plat_map_pack) */
	plat_stat_t st0 = plat_stat(path);
	if (!getenv("GW_MAP_ALL") && st0.size > (24u << 20)) return NULL;
	if (!g_game || strncmp(path, g_game, strlen(g_game)) != 0) return NULL;   /* only the game's own files */
	for (int i = 0; i < 64; i++) {
		if (map[i].data && !strcmp(map[i].path, path)) { *size = map[i].size; if (key) *key = map[i].key; return map[i].data; }
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
			/* as the console stores it: SCUMM data decrypted. Read-only, so that the engine
			   writing into a resource used in place is a crash here, not silence */
			map[i].key = 0;
			if (!getenv("GW_NO_INPLACE") && svm_xor69_data(map[i].data, (uint32_t)n)) {   /* as before: compare */
				for (long j = 0; j < n; j++) map[i].data[j] ^= 0x69;
				map[i].key = 0x69;
			}
			if (!getenv("GW_NO_INPLACE") && svm_fixed_key(path)) {
				map[i].key = svm_fixed_key(path);
				for (long j = 0; j < n; j++) map[i].data[j] ^= map[i].key;
			}
			mprotect(map[i].data, n ? n : 1, PROT_READ);
			snprintf(map[i].path, sizeof map[i].path, "%s", path);
			map[i].size = (uint32_t)n;
			*size = map[i].size;
			if (key) *key = map[i].key;
			return map[i].data;
		}
	}
	return NULL;
}

bool plat_mkdir(const char *path) { return mkdir(path, 0777) == 0; }
void *plat_fopen(const char *path, const char *mode) { return fopen(mode[0] == 'r' ? ci(path) : path, mode); }
uint32_t plat_fread(void *buf, uint32_t n, void *f) { wdog_refresh(); return (uint32_t)fread(buf, 1, n, f); }
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
	if (getenv("GW_RGLOAD") && !atoi(getenv("GW_RGLOAD"))) {
		gw_rg_load(rg_path());
		if (gw_rg_load_at_start()) { args[n++] = "-x"; args[n++] = "99"; }
	}
	args[n] = NULL;
	return gw_scummvm_main(n, args);
}

/* src/pack.c, as the console does it (platform_core.c, cache_pack()): made once next to the data
   file, then mapped outside the heap */
static void host_progress(int pct) { fprintf(stderr, "\rpack: %d%%", pct); if (pct >= 100) fputc('\n', stderr); }

/* The pack is mapped in pieces as on the console (svm_pack_split), each in a mapping of its
   own followed by an unreadable page: an access through the wrong piece, or past the end of
   one, crashes here instead of reading the neighbour. GW_PACK_WHOLE=1: one mapping. */
static struct { char path[512]; const pack_head_t *head; int pieces; uint32_t start[SVM_PACK_MAX_PIECES]; const uint8_t *at[SVM_PACK_MAX_PIECES]; } packs[2];

const void *plat_pack_ptr(const pack_head_t *h, uint32_t off)
{
	for (int i = 0; i < 2; i++)
		if (packs[i].head == h) {
			int k = packs[i].pieces - 1;
			while (k > 0 && off < packs[i].start[k]) k--;
			return packs[i].at[k] + (off - packs[i].start[k]);
		}
	return (const uint8_t *)h + off;
}

static const uint8_t *map_piece(FILE *f, uint32_t start, uint32_t len)
{
	long pg = sysconf(_SC_PAGESIZE);
	size_t span = ((len + pg - 1) / pg + 1) * pg;
	uint8_t *m = (uint8_t *)mmap(0, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED) return NULL;
	if (fseek(f, start, SEEK_SET) || fread(m, 1, len, f) != len) return NULL;
	mprotect(m, span - pg, PROT_READ);       /* resources used in place are read-only, as in flash */
	mprotect(m + span - pg, pg, PROT_NONE);
	return m;
}

const void *plat_map_pack(const char *path)
{
	char own[1024];
	snprintf(own, sizeof own, "%s", ci(path));   /* ci()'s buffer is reused by the calls below */
	path = own;
	for (int i = 0; i < 2; i++) if (packs[i].head && !strcmp(packs[i].path, path)) return packs[i].head;
	plat_stat_t st = plat_stat(path);
	if (getenv("GW_MAP_ALL") || st.size <= (24u << 20)) return NULL;
	if (!svm_pack_candidate(path, st.size)) return NULL;
	char dst[600];
	snprintf(dst, sizeof dst, "%s.gwpack", path);
	pack_head_t h;
	plat_stat_t ps = plat_stat(dst);
	int valid = 0;
	FILE *f = ps.exists ? fopen(dst, "rb") : NULL;
	if (f) { valid = fread(&h, sizeof h, 1, f) == 1 && svm_pack_valid(&h, ps.size, path, st.size); fclose(f); }
	if (!valid) {
		fprintf(stderr, "pack: making %s\n", dst);
		if (!svm_pack_build(path, st.size, dst, host_progress)) return NULL;
		ps = plat_stat(dst);
		f = fopen(dst, "rb");
		if (!f || fread(&h, sizeof h, 1, f) != 1) { if (f) fclose(f); return NULL; }
		fclose(f);
	}
	int slot = -1;
	for (int i = 0; i < 2; i++) if (!packs[i].head) { slot = i; break; }
	if (slot < 0) return NULL;
	f = fopen(dst, "rb");
	if (!f) return NULL;
	int n;
	if (getenv("GW_PACK_WHOLE")) { n = 1; packs[slot].start[0] = 0; }
	else n = svm_pack_split(f, &h, packs[slot].start, SVM_PACK_MAX_PIECES);
	for (int k = 0; k < n; k++) {
		uint32_t end = k + 1 < n ? packs[slot].start[k + 1] : h.total;
		packs[slot].at[k] = map_piece(f, packs[slot].start[k], end - packs[slot].start[k]);
		if (!packs[slot].at[k]) n = 0;
	}
	fclose(f);
	if (!n) return NULL;
	fprintf(stderr, "pack: %s in %d pieces\n", dst, n);
	snprintf(packs[slot].path, sizeof packs[slot].path, "%s", path);
	packs[slot].pieces = n;
	packs[slot].head = (const pack_head_t *)packs[slot].at[0];
	return packs[slot].head;
}
