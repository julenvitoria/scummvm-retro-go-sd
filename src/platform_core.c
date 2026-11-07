/*
 * platform.h on the Game & Watch (Retro-Go SD firmware, external core ABI v2).
 *
 * Resident (RAM image): it runs before the XIP image is mapped (error screens) and while the
 * firmware writes the external flash (caching the game's files). Firmware functions are called
 * by their normal names; the build renames them to the bridge's core_* trampolines.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_input.h"
#include "gw_lcd.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "rg_storage.h"
#include "common.h"
#include "appid.h"
#include "ff.h"
#include "gw_core_bridge.h"
#include "platform.h"

extern uint8_t __RAM_EMU_END__[];
extern int svm_boot_error;
extern char svm_core_path[];
void heap_add(void *mem, size_t bytes);
void heap_add_to(void *mem, size_t bytes, int kind);   /* 0 big, 1 small (heap.c) */
uint32_t heap_total(void);
int gw_scummvm_main(int argc, const char *const argv[]);
bool rg_storage_mkdir(const char *dir);

#define FIRMWARE_RESERVE (16 * 1024)    /* RAM_EMU left at the top for the firmware's ram_malloc() */
#define STACK_SIZE (48 * 1024)          /* ScummVM's stack, in DTCM (the firmware's is 24 KB: too small) */
#define STACK_PAINT 0x5354434Bu
#define SAVE_DIR "/saves/scummvm"
#define LCD_W 320
#define LCD_H 240

static char g_game[256];
static uint32_t g_clut[256];
static void stack_report(void);
static const uint8_t *g_last;           /* last frame shown, for the PAUSE menu's repaint */
static int g_last_pitch, g_last_w, g_last_h;

/* ---------------------------------------------------------------- screen */

static void draw_frame(uint8_t *fb, const uint8_t *pix, int pitch, int w, int h)
{
	if (w <= LCD_W && h <= LCD_H) {
		int x0 = (LCD_W - w) / 2, y0 = (LCD_H - h) / 2;
		if (y0) { memset(fb, 0, y0 * LCD_W); memset(fb + (y0 + h) * LCD_W, 0, (LCD_H - y0 - h) * LCD_W); }
		for (int y = 0; y < h; y++) {
			uint8_t *d = fb + (y0 + y) * LCD_W;
			if (x0) { memset(d, 0, x0); memset(d + x0 + w, 0, LCD_W - x0 - w); }
			memcpy(d + x0, pix + y * pitch, w);
		}
	} else {                               /* 640x400/480 games: every other pixel */
		int sw = w / 2, sh = h / 2;
		int x0 = (LCD_W - sw) / 2, y0 = (LCD_H - sh) / 2;
		memset(fb, 0, LCD_W * LCD_H);
		for (int y = 0; y < sh && y0 + y < LCD_H; y++)
			for (int x = 0; x < sw && x0 + x < LCD_W; x++) fb[(y0 + y) * LCD_W + x0 + x] = pix[2 * y * pitch + 2 * x];
	}
}

static void repaint(void)
{
	uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
	if (g_last) draw_frame(fb, g_last, g_last_pitch, g_last_w, g_last_h);
	else memset(fb, 0, LCD_W * LCD_H);
}

void plat_set_palette(const uint8_t *rgb, int start, int count)
{
	for (int i = 0; i < count && start + i < 256; i++)
		g_clut[start + i] = ((uint32_t)rgb[3 * i] << 16) | ((uint32_t)rgb[3 * i + 1] << 8) | rgb[3 * i + 2];
	lcd_set_clut(g_clut, 256);
}

void plat_present(const uint8_t *pixels, int pitch, int w, int h)
{
	wdog_refresh();
	lcd_sleep_while_swap_pending();
	g_last = pixels; g_last_pitch = pitch; g_last_w = w; g_last_h = h;
	static uint32_t frames;
	if ((++frames & 255) == 0) stack_report();
	draw_frame((uint8_t *)lcd_get_active_buffer(), pixels, pitch, w, h);
	common_ingame_overlay();
	lcd_swap();
}

/* ---------------------------------------------------------------- stack */

/* ScummVM nests deeply and keeps large objects on the stack (a config domain alone is ~700
   bytes): the firmware's 24 KB stack overflows into its MPU red zone (Memfault) before the
   game even starts. It runs on its own, larger stack instead; painted, to report how deep it
   went. */
static uint32_t *g_stack;
static uint32_t g_stack_peak_reported;

__attribute__((naked, noinline)) static int run_on_stack(int argc, const char *const *argv,
                                                        int (*fn)(int, const char *const *), void *top)
{
	__asm volatile(
		"push {r4, lr}\n"
		"mov r4, sp\n"
		"mov sp, r3\n"
		"blx r2\n"
		"mov sp, r4\n"
		"pop {r4, pc}\n");
}

static void stack_report(void)
{
	if (!g_stack) return;
	uint32_t n = STACK_SIZE / 4, i = 0;
	while (i < n && g_stack[i] == STACK_PAINT) i++;
	uint32_t used = (n - i) * 4;
	if (used > g_stack_peak_reported + 1024) {
		g_stack_peak_reported = used;
		printf("ScummVM: stack peak %lu of %u KB\n", (unsigned long)(used / 1024), STACK_SIZE / 1024);
	}
}

/* ---------------------------------------------------------------- input */

uint32_t plat_buttons(void)
{
	static odroid_dialog_choice_t options[] = { ODROID_DIALOG_CHOICE_LAST };
	odroid_gamepad_state_t js;
	wdog_refresh();
	odroid_input_read_gamepad(&js);
	bool menu = js.values[ODROID_INPUT_VOLUME];
	common_emu_input_loop(&js, options, &repaint);       /* PAUSE/SET menu, power, volume... */
	if (menu) lcd_set_clut(g_clut, 256);                 /* the menu used part of the CLUT */
	uint32_t b = 0;
	if (js.values[ODROID_INPUT_UP]) b |= PLAT_UP;
	if (js.values[ODROID_INPUT_DOWN]) b |= PLAT_DOWN;
	if (js.values[ODROID_INPUT_LEFT]) b |= PLAT_LEFT;
	if (js.values[ODROID_INPUT_RIGHT]) b |= PLAT_RIGHT;
	if (js.values[ODROID_INPUT_A]) b |= PLAT_A;
	if (js.values[ODROID_INPUT_B]) b |= PLAT_B;
	if (js.values[ODROID_INPUT_START]) b |= PLAT_GAME;
	if (js.values[ODROID_INPUT_SELECT]) b |= PLAT_TIME;
	return b;
}

/* ---------------------------------------------------------------- time */

uint32_t plat_millis(void) { wdog_refresh(); return HAL_GetTick(); }

void plat_delay(uint32_t ms)
{
	uint32_t t0 = HAL_GetTick();
	while (HAL_GetTick() - t0 < ms) {
		wdog_refresh();
		cpumon_sleep();
	}
}

void plat_date(int *year, int *mon, int *mday, int *hour, int *min, int *sec, int *wday)
{
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	*year = t->tm_year; *mon = t->tm_mon; *mday = t->tm_mday;
	*hour = t->tm_hour; *min = t->tm_min; *sec = t->tm_sec; *wday = t->tm_wday;
}

/* ---------------------------------------------------------------- files */

const char *plat_game_path(void) { return g_game; }
const char *plat_save_path(void) { return SAVE_DIR; }
void plat_log(const char *msg) { printf("%s", msg); }

plat_stat_t plat_stat(const char *path)
{
	rg_stat_t s = rg_storage_stat(path);
	plat_stat_t r = { s.exists, s.is_dir, (uint32_t)s.size };
	return r;
}

bool plat_list_dir(const char *path, void (*cb)(const char *, bool, uint32_t, void *), void *arg)
{
	DIR d;
	FILINFO fi;
	if (f_opendir(&d, path) != FR_OK) return false;
	while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
		wdog_refresh();
		cb(fi.fname, (fi.fattrib & AM_DIR) != 0, (uint32_t)fi.fsize, arg);
	}
	f_closedir(&d);
	return true;
}

bool plat_mkdir(const char *path) { return rg_storage_mkdir(path); }
void *plat_fopen(const char *path, const char *mode) { wdog_refresh(); return fopen(path, mode); }
uint32_t plat_fread(void *buf, uint32_t n, void *f) { wdog_refresh(); return (uint32_t)fread(buf, 1, n, (FILE *)f); }
uint32_t plat_fwrite(const void *buf, uint32_t n, void *f) { wdog_refresh(); return (uint32_t)fwrite(buf, 1, n, (FILE *)f); }
bool plat_fseek(void *f, int32_t off, int whence) { return fseek((FILE *)f, off, whence) == 0; }
int32_t plat_ftell(void *f) { return (int32_t)ftell((FILE *)f); }
void plat_fclose(void *f) { fclose((FILE *)f); }

/* The game's files, cached in external flash before ScummVM starts (the firmware unmaps the
   flash while it writes it, and most of ScummVM runs from there). */
#define MAX_MAPPED 64
static struct { char name[64]; const uint8_t *data; uint32_t size; } g_map[MAX_MAPPED];
static int g_mapped;

static void cache_one(const char *name, bool is_dir, uint32_t size, void *arg)
{
	(void)arg;
	size_t n = strlen(name);
	if (is_dir || g_mapped >= MAX_MAPPED || size == 0 || n >= sizeof g_map[0].name) return;
	if (n > 8 && !strcasecmp(name + n - 8, ".scummvm")) return;
	char path[320];
	snprintf(path, sizeof path, "%s/%s", g_game, name);
	uint32_t got = 0;
	uint8_t *p = odroid_overlay_cache_file_in_flash(path, &got, false);
	if (!p) return;                          /* no room: read from the SD card */
	strcpy(g_map[g_mapped].name, name);
	g_map[g_mapped].data = p;
	g_map[g_mapped].size = got;
	g_mapped++;
}

const uint8_t *plat_map_file(const char *path, uint32_t *size)
{
	size_t gl = strlen(g_game);
	if (strncasecmp(path, g_game, gl) || path[gl] != '/') return NULL;
	for (int i = 0; i < g_mapped; i++)
		if (!strcasecmp(g_map[i].name, path + gl + 1)) { *size = g_map[i].size; return g_map[i].data; }
	return NULL;
}

/* ---------------------------------------------------------------- lifetime */

void plat_quit(void) { odroid_system_switch_app(0); }

static void message(const char *text)
{
	static char line[96];
	static odroid_dialog_choice_t opts[] = { { 0, line, "", 1, NULL }, ODROID_DIALOG_CHOICE_LAST };
	strncpy(line, text, sizeof line - 1);       /* may run before the XIP image is mapped */
	odroid_overlay_dialog("ScummVM", opts, 0, &repaint, 0);
}

int svm_main(void)
{
	odroid_system_init(APPID_CORE, 22050);
	odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	lcd_setup_framebuffers(LCD_MODE_LUT8);
	lcd_clear_buffers();

	if (svm_boot_error) {
		static const char *why[] = { "", "Core .bin not found in /cores", "Not enough RAM to start",
		                             "Cannot read the core .bin", "Not enough room in external flash" };
		message(why[svm_boot_error < 5 ? svm_boot_error : 0]);
		plat_quit();
	}

	/* the heap (heap.c): RAM_EMU from the end of our image to the firmware's reserve, the LCD
	   bonus pool of LUT8 mode, and DTCM's core pool (a margin stays for the stack's red zone) */
	uint8_t *heap = (uint8_t *)(((uintptr_t)&__CORE_BSS_END__ + 7) & ~7u);
	uint8_t *end = __RAM_EMU_END__ - FIRMWARE_RESERVE;
	ram_start = (uint32_t)(uintptr_t)end;
	heap_add(heap, end - heap);
	uint8_t *bonus = NULL;
	size_t bonus_size = 0;
	lcd_get_bonus_pool(&bonus, &bonus_size);
	if (bonus) heap_add_to(bonus, bonus_size, 1);
	g_stack = dtc_malloc(STACK_SIZE);
	if (!g_stack) g_stack = malloc(STACK_SIZE);       /* DTCM full: from the heap (AXI SRAM) */
	size_t dtcm = dtc_get_free_size();
	if (dtcm > 16 * 1024) {
		void *p = dtc_malloc(dtcm - 8 * 1024);
		if (p) heap_add_to(p, dtcm - 8 * 1024, 1);
	}
	void svm_run_constructors(void);
	svm_run_constructors();
	extern uint32_t svm_xip_delta;
	printf("ScummVM: heap %lu KB, XIP delta 0x%08lx\n", (unsigned long)(heap_total() / 1024), (unsigned long)svm_xip_delta);

	/* the game: the folder of the file picked in the launcher (e.g. monkey/monkey.scummvm) */
	const char *picked = ACTIVE_FILE ? ACTIVE_FILE->path : "";
	snprintf(g_game, sizeof g_game, "%s", picked);
	char *slash = strrchr(g_game, '/');
	if (slash) *slash = 0;
	rg_storage_mkdir(SAVE_DIR);
	plat_list_dir(g_game, cache_one, NULL);

	/* no music yet: the AdLib emulator alone would take ~180 KB of heap */
	const char *argv[] = { "scummvm", "-p", g_game, "--music-driver=null", "--auto-detect", NULL };
	for (uint32_t i = 0; i < STACK_SIZE / 4; i++) g_stack[i] = STACK_PAINT;
	run_on_stack(5, argv, gw_scummvm_main, (uint8_t *)g_stack + STACK_SIZE);
	plat_quit();
	return 0;
}
