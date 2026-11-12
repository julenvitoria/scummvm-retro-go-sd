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
#include <stdarg.h>
#include <time.h>
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_input.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "odroid_audio.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "rg_storage.h"
#include "common.h"
#include "appid.h"
#include "ff.h"
#include "gw_core_bridge.h"
#include "platform.h"
#include "pack.h"
#include "osk.h"
#include "gw_core_i18n.h"

extern uint8_t __RAM_EMU_END__[];
extern int svm_boot_error;
extern char svm_core_path[];
void heap_add(void *mem, size_t bytes);
void heap_add_to(void *mem, size_t bytes, int kind);   /* 0 big, 1 small (heap.c) */
uint32_t heap_total(void);
int gw_scummvm_main(int argc, const char *const argv[]);
bool rg_storage_mkdir(const char *dir);

#define FIRMWARE_RESERVE (16 * 1024)    /* RAM_EMU left at the top for the firmware's ram_malloc() */
#define STACK_SIZE (36 * 1024)          /* ScummVM's stack, in DTCM (the firmware's 24 KB is too small; the deepest seen is 26 KB) */
#define STACK_PAINT 0x5354434Bu
#define SCREEN_HEAP (160 * 1024)        /* SCUMM screen buffers: 2 x 64 KB main + verb, text, banner */
#define SAVE_DIR "/saves/scummvm"
#define LCD_W 320
#define LCD_H 240

static char g_game[256];
static uint32_t g_clut[256];           /* the game's palette */
/* What the LCD gets: the firmware stamps its menu/HUD colours (white, greys...) over CLUT slots
   [LCD_OVERLAY_CLUT_BASE, +LCD_OVERLAY_CLUT_MAX) while the PAUSE menu or the volume/brightness
   bars are up, so a game pixel there flashed into a menu colour. Frames never use those slots:
   their colours move to spare slots (exact duplicates elsewhere in the palette), or failing
   that to the nearest colour, through g_lut. */
static uint32_t g_hw[256];
static uint8_t g_lut[256];
#define OV0 LCD_OVERLAY_CLUT_BASE
#define OV1 (LCD_OVERLAY_CLUT_BASE + LCD_OVERLAY_CLUT_MAX)
static inline int in_ov(int i) { return i >= OV0 && i < OV1; }
static void stack_report(void);
static inline void pump(void);
static const uint8_t *g_last;           /* last frame shown, for the PAUSE menu's repaint */
static int g_last_pitch, g_last_w, g_last_h;

/* ---------------------------------------------------------------- screen */

/* the palette's darkest colour, for the bands around a picture smaller than the screen (index 0
   is not black in every game: grey in the NES Maniac Mansion) */
static uint8_t g_border;

static void draw_frame(uint8_t *fb, const uint8_t *pix, int pitch, int w, int h)
{
	const uint8_t B = g_border;
	if (w <= LCD_W && h <= LCD_H) {
		int x0 = (LCD_W - w) / 2, y0 = (LCD_H - h) / 2;
		if (y0) { memset(fb, B, y0 * LCD_W); memset(fb + (y0 + h) * LCD_W, B, (LCD_H - y0 - h) * LCD_W); }
		const uint8_t *L = g_lut;
		for (int y = 0; y < h; y++) {
			uint8_t *d = fb + (y0 + y) * LCD_W;
			if (x0) { memset(d, B, x0); memset(d + x0 + w, B, LCD_W - x0 - w); }
			const uint8_t *src = pix + y * pitch;
			d += x0;
			int x = 0;
			for (; x + 4 <= w; x += 4) {
				uint32_t v = *(const uint32_t *)(src + x);   /* 4 at a time, through the LUT */
				*(uint32_t *)(d + x) = L[v & 255] | (uint32_t)L[(v >> 8) & 255] << 8 |
				                       (uint32_t)L[(v >> 16) & 255] << 16 | (uint32_t)L[v >> 24] << 24;
			}
			for (; x < w; x++) d[x] = L[src[x]];
		}
	} else {                               /* 640x400/480 games: every other pixel */
		int sw = w / 2, sh = h / 2;
		int x0 = (LCD_W - sw) / 2, y0 = (LCD_H - sh) / 2;
		memset(fb, B, LCD_W * LCD_H);
		for (int y = 0; y < sh && y0 + y < LCD_H; y++)
			for (int x = 0; x < sw && x0 + x < LCD_W; x++) fb[(y0 + y) * LCD_W + x0 + x] = g_lut[pix[2 * y * pitch + 2 * x]];
	}
}

static void repaint(void)
{
	uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
	if (g_last) draw_frame(fb, g_last, g_last_pitch, g_last_w, g_last_h);
	else memset(fb, 0, LCD_W * LCD_H);
	osk_draw(fb, g_hw);
}

void plat_redraw(void) { if (g_last) plat_present(g_last, g_last_pitch, g_last_w, g_last_h); }

static uint32_t dist(uint32_t a, uint32_t b)
{
	int dr = (int)((a >> 16) & 255) - (int)((b >> 16) & 255), dg = (int)((a >> 8) & 255) - (int)((b >> 8) & 255),
	    db = (int)(a & 255) - (int)(b & 255);
	return (uint32_t)(dr * dr * 3 + dg * dg * 4 + db * db * 2);
}

static void build_hw_clut(void)
{
	uint8_t spare_of[256] = { 0 };          /* 1: slot given to an overlay colour; 2: a twin, kept */
	for (int i = 0; i < 256; i++) { g_hw[i] = g_clut[i]; g_lut[i] = (uint8_t)i; }
	int s = 255;
	for (int w = OV0; w < OV1; w++) {
		/* the same colour elsewhere: nothing to move */
		int same = -1;
		for (int j = 0; j < 256 && same < 0; j++)
			if (!in_ov(j) && spare_of[j] != 1 && g_clut[j] == g_clut[w]) same = j;
		if (same >= 0) { g_lut[w] = (uint8_t)same; spare_of[same] = 2; continue; }
		/* a spare: a slot whose colour another (kept) slot has too; its pixels go to that one */
		int spare = -1, twin = -1;
		for (; s >= 0 && spare < 0; s--) {
			if (in_ov(s) || spare_of[s]) continue;
			for (int j = 0; j < s; j++)
				if (!in_ov(j) && spare_of[j] != 1 && g_clut[j] == g_clut[s]) { spare = s; twin = j; break; }
		}
		if (spare >= 0) {
			spare_of[spare] = 1;
			spare_of[twin] = 2;
			g_lut[spare] = (uint8_t)twin;
			g_hw[spare] = g_clut[w];
			g_lut[w] = (uint8_t)spare;
		} else {                             /* none left: the nearest colour outside */
			uint32_t best = ~0u;
			for (int j = 0; j < 256; j++)
				if (!in_ov(j) && spare_of[j] != 1 && dist(g_clut[j], g_clut[w]) < best) { best = dist(g_clut[j], g_clut[w]); g_lut[w] = (uint8_t)j; }
		}
	}
	uint32_t best = ~0u;
	for (int i = 0; i < 256; i++) {
		if (in_ov(i)) continue;
		uint32_t c = g_hw[i], l = ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 4 + (c & 255) * 2;
		if (l < best) { best = l; g_border = (uint8_t)i; }
	}
}

void plat_set_palette(const uint8_t *rgb, int start, int count)
{
	for (int i = 0; i < count && start + i < 256; i++)
		g_clut[start + i] = ((uint32_t)rgb[3 * i] << 16) | ((uint32_t)rgb[3 * i + 1] << 8) | rgb[3 * i + 2];
	build_hw_clut();
	lcd_set_clut(g_hw, 256);
}

static void log_save_if_new(void);

void plat_present(const uint8_t *pixels, int pitch, int w, int h)
{
	log_save_if_new();
	wdog_refresh();
	lcd_sleep_while_swap_pending();
	g_last = pixels; g_last_pitch = pitch; g_last_w = w; g_last_h = h;
	static uint32_t frames;
	if ((++frames & 255) == 0) stack_report();
	draw_frame((uint8_t *)lcd_get_active_buffer(), pixels, pitch, w, h);
	osk_draw((uint8_t *)lcd_get_active_buffer(), g_hw);
	common_ingame_overlay();
	lcd_swap();
	pump();
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

/* ---------------------------------------------------------------- sound */

/* The firmware's PCM ring (pcm_attach): its SAI interrupt takes samples from the tail, the
   mixer (gw_system.cpp, through the pump) adds them at the head. ~0.19 s at 22050 Hz. */
#define AUDIO_RATE 22050
#define RING_SAMPLES 4096                         /* power of two */
static int16_t g_ring[RING_SAMPLES];
static volatile uint16_t g_head, g_tail;
static void (*g_pump)(void);
static bool g_audio_on;

void plat_set_pump(void (*fn)(void)) { g_pump = fn; }
uint32_t plat_audio_rate(void) { return AUDIO_RATE; }
int plat_audio_free(void) { return RING_SAMPLES - 1 - ((uint16_t)(g_head - g_tail) & (RING_SAMPLES - 1)); }
void plat_audio_write(const int16_t *s, int n)
{
	uint16_t h = g_head;
	for (int i = 0; i < n; i++) { g_ring[h] = s[i]; h = (h + 1) & (RING_SAMPLES - 1); }
	__asm volatile("dmb" ::: "memory");
	g_head = h;
}

static void audio_start(void)
{
	odroid_audio_init(AUDIO_RATE);
	audio_start_playing(AUDIO_RATE / 60);
	pcm_attach(g_ring, RING_SAMPLES, &g_head, &g_tail);
	pcm_audio_enable(1);
	pcm_audio_set(common_emu_sound_get_volume(), 1);
	g_audio_on = true;
}

static void audio_stop(void)
{
	if (!g_audio_on) return;
	pcm_audio_set(0, 0);
	pcm_audio_enable(0);
	g_audio_on = false;
}

static inline void pump(void) { if (g_pump) g_pump(); }

/* ---------------------------------------------------------------- input */

/* The PAUSE/SET menu's Subtitles option, in games with speech: what Ctrl+T (the keyboard's Txt)
   cycles through. The engine's side is in patches/scummvm-slim.patch (saveload.cpp). */
int gw_scumm_voice_mode(void);                       /* 0 voice, 1 voice and text, 2 text; -1 none */
void gw_scumm_set_voice_mode(int mode);

static const gw_i18n_entry_t i18n_subtitles[] = { { "en", "Subtitles" }, { "es", "Subtítulos" }, GW_I18N_END };
static const gw_i18n_entry_t i18n_voice[] = { { "en", "Voice only" }, { "es", "Solo voz" }, GW_I18N_END };
static const gw_i18n_entry_t i18n_both[] = { { "en", "Voice + text" }, { "es", "Voz y texto" }, GW_I18N_END };
static const gw_i18n_entry_t i18n_text[] = { { "en", "Text only" }, { "es", "Solo texto" }, GW_I18N_END };
static char g_voice_value[32];

static void voice_value(char *out, int mode)
{
	const gw_i18n_entry_t *t = mode == 0 ? i18n_voice : mode == 2 ? i18n_text : i18n_both;
	snprintf(out, 16, "%s", gw_i18n(t));
}

/* the menu draws a copy of the value it was given at open (odroid_overlay_dialog): the new one
   goes into option->value, the copy */
static bool subtitles_cb(odroid_dialog_choice_t *option, odroid_dialog_event_t event, uint32_t repeat)
{
	(void)repeat;
	int mode = gw_scumm_voice_mode();
	if (mode < 0) return false;
	/* in menu order: voice and text, text only, voice only */
	if (event == ODROID_DIALOG_NEXT || event == ODROID_DIALOG_ENTER) mode = (mode + 1) % 3;
	else if (event == ODROID_DIALOG_PREV) mode = (mode + 2) % 3;
	else { voice_value(option->value, mode); return false; }
	gw_scumm_set_voice_mode(mode);
	voice_value(option->value, mode);
	return false;
}

uint32_t plat_buttons(void)
{
	static odroid_dialog_choice_t options[] = {
		{ 400, "", g_voice_value, 1, subtitles_cb },
		ODROID_DIALOG_CHOICE_LAST,
	};
	static int talkie = -2;                            /* -2: not asked yet */
	if (talkie == -2 || talkie == -1) {
		int mode = gw_scumm_voice_mode();             /* -1 until the engine is up, and without speech */
		if (mode >= 0) { talkie = 1; options[0].label = gw_i18n(i18n_subtitles); voice_value(g_voice_value, mode); }
		else talkie = -1;
	}
	odroid_dialog_choice_t *menu_options = talkie == 1 ? options : options + 1;
	odroid_gamepad_state_t js;
	wdog_refresh();
	odroid_input_read_gamepad(&js);
	pump();
	bool menu = js.values[ODROID_INPUT_VOLUME];
	if (menu) pcm_audio_set(0, 0);                       /* silence while the menu is open */
	common_emu_input_loop(&js, menu_options, &repaint);  /* PAUSE/SET menu, power, volume... */
	if (menu) lcd_set_clut(g_hw, 256);                 /* the menu used part of the CLUT */
	if (g_audio_on) pcm_audio_set(common_emu_sound_get_volume(), 1);   /* the menu may change it */
	uint32_t b = 0;
	if (js.values[ODROID_INPUT_UP]) b |= PLAT_UP;
	if (js.values[ODROID_INPUT_DOWN]) b |= PLAT_DOWN;
	if (js.values[ODROID_INPUT_LEFT]) b |= PLAT_LEFT;
	if (js.values[ODROID_INPUT_RIGHT]) b |= PLAT_RIGHT;
	if (js.values[ODROID_INPUT_A]) b |= PLAT_A;
	if (js.values[ODROID_INPUT_B]) b |= PLAT_B;
	/* the firmware's names: GAME is START, TIME is SELECT; a Zelda unit's own START and
	   SELECT buttons are X and Y */
	if (js.values[ODROID_INPUT_START]) b |= PLAT_GAME;
	if (js.values[ODROID_INPUT_SELECT]) b |= PLAT_TIME;
	if (js.values[ODROID_INPUT_X]) b |= PLAT_START;
	if (js.values[ODROID_INPUT_Y]) b |= PLAT_SELECT;
	return b;
}

/* ---------------------------------------------------------------- time */

uint32_t plat_millis(void) { wdog_refresh(); return HAL_GetTick(); }

void plat_delay(uint32_t ms)
{
	uint32_t t0 = HAL_GetTick();
	while (HAL_GetTick() - t0 < ms) {
		wdog_refresh();
		pump();
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
/* ScummVM's log, also kept in RAM: plat_quit() writes it to the SD card (scummvm.log in the
   save folder), so a game that drops back to the launcher says why */
#define LOG_SIZE 4096
static char g_log[LOG_SIZE];
static uint32_t g_log_len;                       /* total written; the ring keeps the last 4 KB */

void plat_log(const char *msg)
{
	printf("%s", msg);
	for (const char *p = msg; *p; p++) g_log[g_log_len++ % LOG_SIZE] = *p;
}

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
	char buf[320];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	plat_log(buf);
}

static uint32_t g_log_saved, g_log_saved_at;

static void log_save(void)
{
	FILE *f = fopen(SAVE_DIR "/scummvm.log", "w");
	if (!f) return;
	uint32_t n = g_log_len < LOG_SIZE ? g_log_len : LOG_SIZE, start = g_log_len - n;
	for (uint32_t i = 0; i < n; i++) fputc(g_log[(start + i) % LOG_SIZE], f);
	fclose(f);
	g_log_saved = g_log_len;
}

/* while the game runs (plat_present): new lines reach the card within a few seconds, so the
   log is there too when the console is switched off instead of leaving the game */
static void log_save_if_new(void)
{
	uint32_t now = HAL_GetTick();
	if (g_log_len == g_log_saved || now - g_log_saved_at < 3000) return;
	g_log_saved_at = now;
	log_save();
}

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
void *plat_fopen(const char *path, const char *mode)
{
	wdog_refresh();
	FILE *f = fopen(path, mode);
	if (!f && mode[0] == 'r') logf_("ScummVM: cannot open %s\n", path);
	return f;
}
uint32_t plat_fread(void *buf, uint32_t n, void *f) { wdog_refresh(); return (uint32_t)fread(buf, 1, n, (FILE *)f); }
uint32_t plat_fwrite(const void *buf, uint32_t n, void *f) { wdog_refresh(); return (uint32_t)fwrite(buf, 1, n, (FILE *)f); }
bool plat_fseek(void *f, int32_t off, int whence) { return fseek((FILE *)f, off, whence) == 0; }
int32_t plat_ftell(void *f) { return (int32_t)ftell((FILE *)f); }
void plat_fclose(void *f) { fclose((FILE *)f); }

/* The game's files, cached in external flash before ScummVM starts (the firmware unmaps the
   flash while it writes it, and most of ScummVM runs from there). */
#define MAX_MAPPED 160                    /* Indiana Jones 3 VGA: ~95 files, one per room */
#define MAX_CACHED_FILE (24u << 20)
static struct { char name[28]; const uint8_t *data; uint32_t size; uint8_t key, fixed; } g_map[MAX_MAPPED];
static int g_mapped, g_files;

/* Called by the firmware with each 16 KB of a file on its way into flash (OSPI is unmapped
   then: this must stay resident, and it is -- platform_core.o is in the RAM image). SCUMM
   v4-v6 data files go in decrypted, so their resources can be used where they lie. */
uint8_t *odroid_overlay_cache_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p, bool byte_swap,
                                                     void (*relocate_cb)(uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t));
static uint8_t g_decrypting, g_fixed_key;           /* g_fixed_key: svm_fixed_key() of the file going in */
static void decrypt_chunk(uint8_t *buf, uint32_t len, uint32_t offset, uint8_t *addr, uint32_t size)
{
	(void)addr; (void)size;
	if (offset == 0) g_decrypting = g_fixed_key ? g_fixed_key : svm_xor69_data(buf, len) ? 0x69 : 0;
	if (!g_decrypting) return;
	uint32_t *w = (uint32_t *)buf, i = 0, k = g_decrypting * 0x01010101u;   /* the buffer is the firmware's, word-aligned */
	for (; i < len / 4; i++) w[i] ^= k;
	for (i *= 4; i < len; i++) buf[i] ^= g_decrypting;
}

/* Data files too big for the flash (Full Throttle's FT.LA1, Beneath a Steel Sky's sky.dsk):
   their resource packs (src/pack.c), made on the card the first time and put in the flash in
   pieces (svm_pack_split), each where there is room for it */
/* the firmware's blob store (Core/Inc/gw_flash_alloc.h, through the bridge) */
typedef struct {
	uint32_t key_crc, flash_address, prog_addr, erase_addr, erase_left, total, done, erase_size_total;
	bool active;
} flash_stream_t;
const uint8_t *lookup_data_in_flash(const char *key, uint32_t *size_out);
bool store_data_begin(flash_stream_t *st, const char *key, uint32_t total_size);
bool store_data_append(flash_stream_t *st, const uint8_t *buf, uint32_t len);
const uint8_t *store_data_finish(flash_stream_t *st);
void store_data_abort(flash_stream_t *st);

#define MAX_PACKS 2
static struct {
	char name[28];
	const pack_head_t *head;
	int pieces;
	uint32_t start[SVM_PACK_MAX_PIECES];
	const uint8_t *at[SVM_PACK_MAX_PIECES];
} g_pack[MAX_PACKS];
static int g_packs;

/* a progress bar: drawn into the frame buffer and shown (lcd_swap), as the firmware's own
   "caching" bar does -- without the swap nothing was seen, and a minutes-long copy looked
   like a hang with the previous bar full on screen */
static void show_bar(const char *header, int pct)
{
	if (lcd_is_swap_pending()) return;
	odroid_overlay_draw_progress_bar(header, (uint8_t)pct);
	lcd_swap();
}
static const gw_i18n_entry_t i18n_preparing[] = {
	{ "en", "Preparing the game (first time only)" }, { "es", "Preparando el juego (solo la primera vez)" },
	{ "fr", "Préparation du jeu (1re fois seulement)" }, { "de", "Spiel wird vorbereitet (nur beim 1. Mal)" },
	{ "it", "Preparazione del gioco (solo la 1a volta)" }, { "pt", "A preparar o jogo (só a 1.ª vez)" }, GW_I18N_END };
static const gw_i18n_entry_t i18n_copying[] = {
	{ "en", "Copying the game to the flash" }, { "es", "Copiando el juego a la flash" },
	{ "fr", "Copie du jeu dans la flash" }, { "de", "Spiel wird in den Flash kopiert" },
	{ "it", "Copia del gioco nella flash" }, { "pt", "A copiar o jogo para a flash" }, GW_I18N_END };
static void pack_progress(int pct) { show_bar(gw_i18n(i18n_preparing), pct); }

/* piece k of the pack in the flash, by key (the firmware's blob store): put there from the
   card when it is not there yet. The key carries what the pack was made from, so a new pack
   (the game's files changed) does not find the old pieces. */
static const uint8_t *pack_piece(void *f, const char *name, const pack_head_t *h, uint32_t start, uint32_t len,
                                 int k, uint32_t *done, uint32_t total)
{
	char key[96];
	snprintf(key, sizeof key, "svm-pack|%s|%lx|%lx|%lx|%d", name, (unsigned long)h->src_size,
	         (unsigned long)h->src_sum, (unsigned long)h->total, k);
	uint32_t got = 0;
	const uint8_t *p = lookup_data_in_flash(key, &got);
	if (p && got == len) { *done += len; return p; }
	static flash_stream_t st;                 /* (the firmware's: the caller owns it) */
	uint8_t *buf = (uint8_t *)malloc(16384);
	if (!buf) return NULL;
	if (!plat_fseek(f, (int32_t)start, 0) || !store_data_begin(&st, key, len)) { free(buf); return NULL; }
	uint8_t last = 0xff;
	for (uint32_t left = len; left;) {
		uint32_t n = left < 16384 ? left : 16384;
		if (plat_fread(buf, n, f) != n || !store_data_append(&st, buf, n)) { store_data_abort(&st); free(buf); return NULL; }
		left -= n;
		*done += n;
		uint8_t pct = (uint8_t)((uint64_t)*done * 100 / total);
		if (pct != last) { show_bar(gw_i18n(i18n_copying), pct); last = pct; }
	}
	free(buf);
	return store_data_finish(&st);
}

static void cache_pack(const char *name, uint32_t size)
{
	char src[320], dst[330];
	snprintf(src, sizeof src, "%s/%s", g_game, name);
	if (g_packs >= MAX_PACKS || strlen(name) >= sizeof g_pack[0].name || !svm_pack_candidate(src, size)) return;
	snprintf(dst, sizeof dst, "%s.gwpack", src);
	pack_head_t h;
	plat_stat_t st = plat_stat(dst);
	int valid = 0;
	if (st.exists && !st.is_dir) {
		void *f = plat_fopen(dst, "rb");
		if (f) {
			valid = plat_fread(&h, sizeof h, f) == sizeof h && svm_pack_valid(&h, st.size, src, size);
			plat_fclose(f);
		}
	}
	if (!valid) {
		logf_("ScummVM: making %s.gwpack (the resources but the sounds, for the flash)\n", name);
		pack_progress(0);
		if (!svm_pack_build(src, size, dst, pack_progress)) {
			logf_("ScummVM: could not make %s.gwpack: its resources are read from the SD card\n", name);
			return;
		}
		void *f = plat_fopen(dst, "rb");
		valid = f && plat_fread(&h, sizeof h, f) == sizeof h;
		if (f) plat_fclose(f);
		if (!valid) return;
	}
	typeof(g_pack[0]) *P = &g_pack[g_packs];
	void *f = plat_fopen(dst, "rb");
	if (!f) return;
	P->pieces = svm_pack_split(f, &h, P->start, SVM_PACK_MAX_PIECES);
	uint32_t done = 0;
	int k = 0;
	for (; k < P->pieces; k++) {
		uint32_t end = k + 1 < P->pieces ? P->start[k + 1] : h.total;
		P->at[k] = pack_piece(f, name, &h, P->start[k], end - P->start[k], k, &done, h.total);
		if (!P->at[k]) break;
	}
	plat_fclose(f);
	if (!P->pieces || k < P->pieces) {
		logf_("ScummVM: no room in external flash for %s.gwpack (piece %d of %d)\n", name, k + 1, P->pieces);
		return;
	}
	strcpy(P->name, name);
	P->head = (const pack_head_t *)P->at[0];
	g_packs++;
	logf_("ScummVM: %s: %lu resources (%lu KB) in external flash, in %d pieces\n", name,
	      (unsigned long)h.count, (unsigned long)(h.total / 1024), P->pieces);
}

const void *plat_map_pack(const char *path)
{
	size_t gl = strlen(g_game);
	if (strncasecmp(path, g_game, gl) || path[gl] != '/') return NULL;
	for (int i = 0; i < g_packs; i++)
		if (!strcasecmp(g_pack[i].name, path + gl + 1)) return g_pack[i].head;
	return NULL;
}

const void *plat_pack_ptr(const pack_head_t *h, uint32_t off)
{
	for (int i = 0; i < g_packs; i++)
		if (g_pack[i].head == h) {
			int k = g_pack[i].pieces - 1;
			while (k > 0 && off < g_pack[i].start[k]) k--;
			return g_pack[i].at[k] + (off - g_pack[i].start[k]);
		}
	return (const uint8_t *)h + off;
}

/* name: relative to the game folder ("FT.LA1", "DATA/ICONS.NUT") */
static void cache_rel(const char *name, uint32_t size)
{
	size_t n = strlen(name);
	if (g_mapped >= MAX_MAPPED || size == 0 || n >= sizeof g_map[0].name) return;
	if (n > 8 && !strcasecmp(name + n - 8, ".scummvm")) return;
	if (n > 7 && !strcasecmp(name + n - 7, ".gwpack")) return;
	if (name[0] == '.') return;              /* macOS's ._files and .DS_Store */
	/* not game data: manuals, icons, the DOS program (no need to fill the flash with them) */
	static const char *const skip[] = { ".pdf", ".ico", ".exe", ".com", ".txt", ".doc", ".bat", ".jpg", ".png", ".nfo" };
	for (unsigned i = 0; i < sizeof skip / sizeof skip[0]; i++)
		if (n > 4 && !strcasecmp(name + n - 4, skip[i])) return;
	g_files++;
	/* bigger than this stays on the SD card (a talkie's speech file is ~200 MB, the flash 64) */
	if (size > MAX_CACHED_FILE) {
		logf_("ScummVM: %s (%lu KB) is read from the SD card\n", name, (unsigned long)(size / 1024));
		cache_pack(name, size);
		return;
	}
	char path[320];
	snprintf(path, sizeof path, "%s/%s", g_game, name);
	/* An encrypted SCUMM data file goes in under "folder//name": the same file to FatFs, but a
	   new key to the firmware's flash cache (path + date + size). A copy cached raw by an
	   older core is then not found, and this one is decrypted on its way in -- copying the
	   file to the card again does not help, since a copy keeps the file's date. */
	uint8_t head[8];
	void *f = plat_fopen(path, "rb");
	if (f) {
		if (plat_fread(head, sizeof head, f) == sizeof head && svm_xor69_data(head, sizeof head))
			snprintf(path, sizeof path, "%s//%s", g_game, name);
		plat_fclose(f);
	}
	/* ...and so does a file kept XORed by name (Soltys' vol.dat), with three slashes */
	g_fixed_key = svm_fixed_key(name);
	if (g_fixed_key) snprintf(path, sizeof path, "%s///%s", g_game, name);
	uint32_t got = 0;
	uint8_t *p = odroid_overlay_cache_file_in_flash_relocate(path, &got, false, decrypt_chunk);
	uint8_t fixed = g_fixed_key;
	g_fixed_key = 0;
	if (!p) return;                          /* no room: read from the SD card */
	g_map[g_mapped].fixed = fixed;
	strcpy(g_map[g_mapped].name, name);
	g_map[g_mapped].data = p;
	g_map[g_mapped].size = got;
	g_mapped++;
}

/* Subfolders (SCUMM v7: DATA/ with the fonts and the bike scenes' tables, VIDEO/ with the
   cutscenes): their .NUT files only (fonts and sprites, which the engine then draws from the
   flash instead of decoding them into memory), after the folder's own. Everything else there
   is read from the card. Not "every small file": Full Throttle has 140 of them, and the
   firmware's flash cache keeps track of a limited number of files per game -- past it, it
   erased files still in use (seen on the console: garbage resources, videos not found). */
#define MAX_SUBDIRS 4
#define MAX_SUBDIR_FILE (256u << 10)
static char g_subdirs[MAX_SUBDIRS][16], g_cur_subdir[16];
static int g_nsubdirs;

static void cache_one(const char *name, bool is_dir, uint32_t size, void *arg)
{
	(void)arg;
	if (is_dir) {
		if (g_nsubdirs < MAX_SUBDIRS && strlen(name) < sizeof g_subdirs[0] && name[0] != '.')
			strcpy(g_subdirs[g_nsubdirs++], name);
		return;
	}
	cache_rel(name, size);
}

static void cache_sub(const char *name, bool is_dir, uint32_t size, void *arg)
{
	(void)arg;
	char rel[sizeof g_map[0].name];
	size_t n = strlen(name);
	if (is_dir || size > MAX_SUBDIR_FILE || n < 5 || strcasecmp(name + n - 4, ".nut") || name[0] == '.') return;
	if ((size_t)snprintf(rel, sizeof rel, "%s/%s", g_cur_subdir, name) >= sizeof rel) return;
	cache_rel(rel, size);
}

/* ---- Broken Sword: its clusters (150 MB) do not all fit in the flash ----
   The ones every part of the game uses go in, then the "zones" (one cluster per part of the
   world) as many as fit, the most recently needed first (SAVE_DIR/sword1_zones.txt). The game
   reaching a room whose zone is not in the flash saves itself between the two rooms and comes
   back here (gw_sword1_zone): the next start puts that zone in and goes on from that save. */
#define BS_ZONES_FILE SAVE_DIR "/sword1_zones.txt"
#define BS_ZONE_SAVE SAVE_DIR "/sword1_zone.sav"
static const char *const bs_fixed[] = { "swordres.rif", "scripts.clu", "compacts.clu", "text.clu", "general.clu", "maps.clu" };
static const char *const bs_zones[] = { "paris1", "paris2", "paris3", "paris4", "ireland", "syria", "spain", "scotland", "train" };
static char g_bs_dir[12];                 /* the clusters' folder under the game's ("clusters/" or "") */
static bool g_bs;

static bool bs_detect(void)
{
	char p[340];
	snprintf(p, sizeof p, "%s/clusters/swordres.rif", g_game);
	if (plat_stat(p).exists) { strcpy(g_bs_dir, "clusters/"); return g_bs = true; }
	snprintf(p, sizeof p, "%s/swordres.rif", g_game);
	if (plat_stat(p).exists) { g_bs_dir[0] = 0; return g_bs = true; }
	return false;
}

/* the external flash's size. The OCTOSPI's DEVSIZE is no help (256 MB on a 64 MB console: the
   firmware sets it larger than the chip, and its own probe is not exported), but a chip read
   past its end wraps around: the smallest n at which this core's code (in the flash) shows
   up again n bytes further on is the size (64 MB when it does not show up). */
int scummvm_main(int argc, const char *const argv[]);
static uint32_t flash_bytes(void)
{
	uint32_t devsize = (*(volatile uint32_t *)0x52005008 >> 16) & 31;   /* OCTOSPI1->DCR1 */
	uint32_t window = devsize >= 23 && devsize <= 28 ? 2u << devsize : 64u << 20;
	const uint8_t *p = (const uint8_t *)((uintptr_t)scummvm_main & ~(uintptr_t)1);
	if ((uintptr_t)p < 0x90000000u || (uintptr_t)p >= 0x90000000u + window) return 64u << 20;
	for (uint32_t n = 16u << 20; n < window; n <<= 1) {
		if ((uintptr_t)p + n + 4096 > 0x90000000u + window) break;
		if (!memcmp(p, p + n, 4096)) return n;
	}
	return 64u << 20;                         /* no wrap seen: the usual size, to be safe */
}

/* the zones in the order they go in: the file's (most recent first), then the rest */
static int bs_zone_order(char order[][12])
{
	int n = 0;
	char buf[160] = "";
	void *f = plat_fopen(BS_ZONES_FILE, "rb");
	if (f) { buf[plat_fread(buf, sizeof buf - 1, f)] = 0; plat_fclose(f); }
	for (char *t = strtok(buf, " \r\n,"); t && n < 9; t = strtok(NULL, " \r\n,"))
		for (unsigned i = 0; i < 9; i++)
			if (!strcasecmp(t, bs_zones[i])) {
				int dup = 0;
				for (int j = 0; j < n; j++) dup |= !strcmp(order[j], bs_zones[i]);
				if (!dup) strcpy(order[n++], bs_zones[i]);
			}
	for (unsigned i = 0; i < 9; i++) {
		int dup = 0;
		for (int j = 0; j < n; j++) dup |= !strcmp(order[j], bs_zones[i]);
		if (!dup) strcpy(order[n++], bs_zones[i]);
	}
	return n;
}

static void cache_sword1(void)
{
	/* what the flash keeps for the game: all of it but the firmware's own reserve and this
	   core (3.8 MB) -- more than really fits would copy a zone on every start in vain. 54 MB
	   of a 64 MB one: the common part and Paris 1 and 2 (52.3 MB) went in. */
	uint32_t budget = flash_bytes() - (10u << 20), used = 0;
	char rel[28], path[340];
	for (unsigned i = 0; i < sizeof bs_fixed / sizeof bs_fixed[0]; i++) {
		snprintf(rel, sizeof rel, "%s%s", g_bs_dir, bs_fixed[i]);
		snprintf(path, sizeof path, "%s/%s", g_game, rel);
		plat_stat_t st = plat_stat(path);
		if (!st.exists) continue;
		used += st.size;
		cache_rel(rel, st.size);
	}
	char order[9][12];
	int n = bs_zone_order(order);
	for (int i = 0; i < n; i++) {
		snprintf(rel, sizeof rel, "%s%s.clu", g_bs_dir, order[i]);
		snprintf(path, sizeof path, "%s/%s", g_game, rel);
		plat_stat_t st = plat_stat(path);
		if (!st.exists || used + st.size > budget) continue;
		used += st.size;
		int had = g_mapped;
		cache_rel(rel, st.size);
		if (g_mapped == had) break;             /* the flash is full after all */
	}
	logf_("ScummVM: Broken Sword: %lu KB of clusters for the flash (of %lu; the flash: %lu MB)\n",
	      (unsigned long)(used / 1024), (unsigned long)(budget / 1024), (unsigned long)(flash_bytes() >> 20));
}

/* patches/scummvm-slim.patch (SwordEngine::gwZoneCheck): the game saved itself (slot 98,
   BS_ZONE_SAVE) on its way into a room of 'cluster', which is not in the flash */
static const gw_i18n_entry_t i18n_bs_zone1[] = {
	{ "en", "Broken Sword: a new part of the game." }, { "es", "Broken Sword: una zona nueva del juego." },
	{ "fr", "Broken Sword : une nouvelle partie du jeu." }, { "de", "Baphomets Fluch: ein neuer Spielabschnitt." },
	{ "it", "Broken Sword: una nuova parte del gioco." }, { "pt", "Broken Sword: uma nova parte do jogo." }, GW_I18N_END };
static const gw_i18n_entry_t i18n_bs_zone2[] = {
	{ "en", "Start the game again: it goes into" }, { "es", "Vuelve a entrar en el juego: se copia" },
	{ "fr", "Relancez le jeu : elle est copiee" }, { "de", "Spiel neu starten: er wird in den" },
	{ "it", "Riavvia il gioco: viene copiata" }, { "pt", "Volta a entrar no jogo: e copiada" }, GW_I18N_END };
static const gw_i18n_entry_t i18n_bs_zone3[] = {
	{ "en", "the flash and you go on from here." }, { "es", "a la flash y sigues desde aqui." },
	{ "fr", "dans la flash, la partie continue ici." }, { "de", "Flash kopiert, es geht hier weiter." },
	{ "it", "nella flash e si continua da qui." }, { "pt", "para a flash e continuas daqui." }, GW_I18N_END };

static void message3(const char *a, const char *b, const char *c);
void gw_sword1_zone(const char *cluster)
{
	char buf[160];
	snprintf(buf, sizeof buf, "%s\n", cluster);    /* first, then the others as they were */
	void *f = plat_fopen(BS_ZONES_FILE, "rb");
	char old[160] = "";
	if (f) { old[plat_fread(old, sizeof old - 1, f)] = 0; plat_fclose(f); }
	for (char *t = strtok(old, " \r\n,"); t; t = strtok(NULL, " \r\n,"))
		if (strcasecmp(t, cluster) && strlen(buf) + strlen(t) + 2 < sizeof buf) { strcat(buf, t); strcat(buf, "\n"); }
	if ((f = plat_fopen(BS_ZONES_FILE, "wb"))) { plat_fwrite(buf, strlen(buf), f); plat_fclose(f); }
	logf_("ScummVM: Broken Sword: %s is not in the flash; saved, for the next start\n", cluster);
	audio_stop();
	log_save();
	message3(gw_i18n(i18n_bs_zone1), gw_i18n(i18n_bs_zone2), gw_i18n(i18n_bs_zone3));
	odroid_system_switch_app(0);
	for (;;) {}
}

static bool bs_zone_save_pending(void)
{
	plat_stat_t st = plat_stat(BS_ZONE_SAVE);
	return st.exists && st.size > 0;
}

static void cache_game(void)
{
	if (bs_detect()) { cache_sword1(); return; }
	plat_list_dir(g_game, cache_one, NULL);
	for (int i = 0; i < g_nsubdirs; i++) {
		char path[320];
		strcpy(g_cur_subdir, g_subdirs[i]);
		snprintf(path, sizeof path, "%s/%s", g_game, g_subdirs[i]);
		plat_list_dir(path, cache_sub, NULL);
	}
}

/* Which copies are decrypted: decided from what is in flash, since a file cached by an older
   core (or before this one decrypted) is a cache hit and still holds the original bytes. */
static void find_keys(void)
{
	int xor_index = 0, stale = 0;
	for (int i = 0; i < g_mapped; i++) xor_index |= svm_xor69_index(g_map[i].data, g_map[i].size);
	for (int i = 0; i < g_mapped; i++) {
		if (g_map[i].fixed) { g_map[i].key = g_map[i].fixed; continue; }
		g_map[i].key = svm_plain_data(g_map[i].data, g_map[i].size, xor_index) ? 0x69 : 0;
		if (svm_xor69_data(g_map[i].data, g_map[i].size)) {
			stale++;
			logf_("ScummVM: %s is in flash as on the card (cached before); it is used as on the card\n", g_map[i].name);
		}
	}
	(void)stale;
}

const uint8_t *plat_map_file(const char *path, uint32_t *size, uint8_t *key)
{
	size_t gl = strlen(g_game);
	if (strncasecmp(path, g_game, gl) || path[gl] != '/') return NULL;
	for (int i = 0; i < g_mapped; i++)
		if (!strcasecmp(g_map[i].name, path + gl + 1)) {
			*size = g_map[i].size;
			if (key) *key = g_map[i].key;
			return g_map[i].data;
		}
	return NULL;                             /* read from the SD card (logged once, by cache_one) */
}

/* the last line of the log, for a message on screen */
static void last_log_line(char *last, size_t size)
{
	uint32_t n = g_log_len < LOG_SIZE ? g_log_len : LOG_SIZE, i = 0, j = g_log_len;
	while (i < n && (g_log[(j - 1) % LOG_SIZE] == '\n' || g_log[(j - 1) % LOG_SIZE] == ' ')) { j--; i++; }
	uint32_t e = j;
	while (i < n && g_log[(j - 1) % LOG_SIZE] != '\n') { j--; i++; }
	uint32_t k = 0;
	for (uint32_t p = j; p < e && k < size - 1; p++) last[k++] = g_log[p % LOG_SIZE];
	last[k] = 0;
}

/* ---------------------------------------------------------------- which game */

/* The launcher lists /roms/scummvm/<Name>.scummvm; the game's files live elsewhere:
 *   - the .scummvm file holds a folder: an absolute path, or a name under /scummvm
 *     ("monkey" -> /scummvm/monkey),
 *   - empty: /scummvm/<Name>, or else the folder the .scummvm file itself is in (the layout of
 *     the first version: /roms/scummvm/monkey/Monkey Island.scummvm). */
#define GAMES_DIR "/scummvm"

/* the optional lines of the .scummvm file */
static char g_opt_game[48], g_opt_platform[40], g_opt_language[40];
static char g_opt_extra[96];             /* extrapath=: a folder of extra files (CD music tracks) */
static char g_arg_extra[320];

static bool is_dir(const char *p) { rg_stat_t s = rg_storage_stat(p); return s.exists && s.is_dir; }

static void find_game(const char *picked)
{
	char text[200] = "";
	FILE *f = fopen(picked, "rb");
	if (f) {
		size_t n = fread(text, 1, sizeof text - 1, f), k = 0;
		fclose(f);
		/* saved as UTF-16 (Windows Notepad's "Unicode"): keep the ASCII bytes */
		bool utf16 = n >= 2 && (((uint8_t)text[0] == 0xFF && (uint8_t)text[1] == 0xFE) ||
		                        ((uint8_t)text[0] == 0xFE && (uint8_t)text[1] == 0xFF));
		for (size_t i = utf16 ? 2 : 0; i < n; i++)
			if (text[i]) text[k++] = text[i];
		text[k] = 0;
	}
	/* line by line: "key=value" lines are options (game=, platform=, language=), the first
	   other line is the folder; trimmed of spaces, quotes, a UTF-8 BOM and a trailing slash */
	char *t = "", *line = text;
	if ((uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB && (uint8_t)line[2] == 0xBF) line += 3;
	bool have_folder = false;
	while (*line) {
		char *next = line + strcspn(line, "\r\n");
		char *after = next + strspn(next, "\r\n");
		*next = 0;
		while (*line == ' ' || *line == '\t' || *line == '"') line++;
		char *e = next;
		while (e > line && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '"' || e[-1] == '/')) e--;
		*e = 0;
		char *eq = strchr(line, '=');
		if (eq) {
			*eq = 0;
			char *k = line, *v = eq + 1;
			char *ke = eq;
			while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
			while (*v == ' ' || *v == '\t') v++;
			if (!strcasecmp(k, "game")) snprintf(g_opt_game, sizeof g_opt_game, "%s", v);
			else if (!strcasecmp(k, "platform")) snprintf(g_opt_platform, sizeof g_opt_platform, "--platform=%s", v);
			else if (!strcasecmp(k, "language")) snprintf(g_opt_language, sizeof g_opt_language, "--language=%s", v);
			else if (!strcasecmp(k, "extrapath")) snprintf(g_opt_extra, sizeof g_opt_extra, "%s", v);
			else logf_("ScummVM: unknown option \"%s\" in %s\n", k, picked);
		} else if (!have_folder && *line) {
			t = line;
			have_folder = true;
		}
		line = after;
	}
	if (g_opt_game[0] || g_opt_platform[0] || g_opt_language[0])
		logf_("ScummVM: options game=%s %s %s\n", g_opt_game, g_opt_platform, g_opt_language);

	logf_("ScummVM: picked %s, it says \"%s\"\n", picked, t);
	if (*t) {
		if (*t == '/') snprintf(g_game, sizeof g_game, "%s", t);
		else snprintf(g_game, sizeof g_game, GAMES_DIR "/%s", t);
		if (is_dir(g_game)) return;
		logf_("ScummVM: no folder %s\n", g_game);
	}
	const char *base = strrchr(picked, '/');
	base = base ? base + 1 : picked;
	snprintf(g_game, sizeof g_game, GAMES_DIR "/%s", base);
	char *dot = strrchr(g_game, '.');
	if (dot && dot > g_game + sizeof GAMES_DIR) *dot = 0;
	if (is_dir(g_game)) return;
	logf_("ScummVM: no folder %s\n", g_game);
	snprintf(g_game, sizeof g_game, "%s", picked);           /* the first layout */
	char *slash = strrchr(g_game, '/');
	if (slash) *slash = 0;
}

/* ---------------------------------------------------------------- savestates */

/* the Retro-Go PAUSE/SET menu's Save/Load and the power-off save (gw_system.cpp) */
int gw_rg_save(const char *path);
int gw_rg_load(const char *path);
int gw_rg_load_at_start(void);

static bool rg_save(const char *path) { return path && gw_rg_save(path); }
static bool rg_load(const char *path) { return path && gw_rg_load(path); }

/* the slot's preview: the game's frame without the menu on top */
static void *rg_screenshot(void)
{
	lcd_wait_for_vblank();
	uint8_t *fb = (uint8_t *)lcd_get_active_buffer();
	if (g_last) draw_frame(fb, g_last, g_last_pitch, g_last_w, g_last_h);
	return fb;
}

/* ---------------------------------------------------------------- lifetime */

static void message3(const char *a, const char *b, const char *c);
static void last_log_line(char *out, size_t size);

/* newlib's _exit (boot.c): exit(), abort() and an uncaught exception such as std::bad_alloc
   end up here instead of in plat_quit() */
void svm_fatal_exit(int code) __attribute__((noreturn));
void plat_fatal(void) { svm_fatal_exit(-1); }      /* error() in ScummVM: its message is the last log line */

void svm_fatal_exit(int code) __attribute__((noreturn));
/* a failed assert() in ScummVM: newlib's would only print to stderr and abort (exit 1) */
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
	const char *base = strrchr(file, '/');
	logf_("assert: %s (%s:%d %s)\n", expr, base ? base + 1 : file, line, func ? func : "");
	svm_fatal_exit(1);
}

/* abort(): what an allocation with new comes to when the heap is out (the heap logged it) */
void abort(void)
{
	logf_("ScummVM: abort\n");
	svm_fatal_exit(1);
}

void svm_fatal_exit(int code)
{
	static int busy;
	if (!busy++) {
		if (code != -1) logf_("ScummVM: stopped (exit %d)\n", code);
		audio_stop();
		if (g_game[0]) log_save();
		char last[64];
		last_log_line(last, sizeof last);
		message3("ScummVM stopped:", last, "(see /saves/scummvm/scummvm.log)");
	}
	odroid_system_switch_app(0);
	for (;;) {}
}

void plat_quit(void)
{
	audio_stop();
	if (g_game[0]) log_save();
	odroid_system_switch_app(0);
}

static void message3(const char *a, const char *b, const char *c)
{
	static char line[3][64];
	static odroid_dialog_choice_t opts[] = { { 0, line[0], "", 1, NULL }, { 1, line[1], "", 1, NULL },
	                                         { 2, line[2], "", 1, NULL }, ODROID_DIALOG_CHOICE_LAST };
	const char *t[3] = { a, b, c };
	int n = 0;
	for (int i = 0; i < 3; i++) {                /* strncpy: may run before the XIP image is mapped */
		if (!t[i]) continue;
		strncpy(line[n], t[i], sizeof line[0] - 1);
		line[n][sizeof line[0] - 1] = 0;
		opts[n].enabled = 1;
		n++;
	}
	odroid_dialog_choice_t last = ODROID_DIALOG_CHOICE_LAST;
	if (n < 3) opts[n] = last;
	odroid_overlay_dialog("ScummVM", opts, 0, &repaint, 0);
}
static void message(const char *text) { message3(text, NULL, NULL); }

int svm_main(void)
{
	odroid_system_init(APPID_CORE, 22050);
	odroid_system_emu_init(&rg_load, &rg_save, &rg_screenshot, NULL, NULL, NULL, NULL);
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
	/* the SCUMM engine's screen buffers get a heap of their own (heap.c, heap_screen_hint) */
	heap_add_to(heap, SCREEN_HEAP, 2);
	heap += SCREEN_HEAP;
	heap_add(heap, end - heap);
	uint8_t *bonus = NULL;
	size_t bonus_size = 0;
	lcd_get_bonus_pool(&bonus, &bonus_size);
	if (bonus) heap_add_to(bonus, bonus_size, 1);
	g_stack = dtc_malloc(STACK_SIZE);
	if (!g_stack) g_stack = malloc(STACK_SIZE);       /* DTCM full: from the heap (AXI SRAM) */
	/* ITCM: 64 KB of zero-wait RAM the firmware leaves entirely free (this core's code is not
	   there); its first bytes are skipped, a heap block at address 0 would read as NULL */
	heap_add_to((void *)0x100, 0x10000 - 0x100, 1);
	/* AHB SRAM: the firmware's own heap; most of what is free, its menus keep 48 KB */
	size_t ahb = ahb_get_free_size();
	if (ahb > 64 * 1024) {
		void *p = ahb_malloc(ahb - 48 * 1024);
		if (p) heap_add_to(p, ahb - 48 * 1024, 1);
	}
	logf_("ScummVM: AHB free %u KB\n", (unsigned)(ahb / 1024));
	size_t dtcm = dtc_get_free_size();
	if (dtcm > 16 * 1024) {
		void *p = dtc_malloc(dtcm - 8 * 1024);
		if (p) heap_add_to(p, dtcm - 8 * 1024, 1);
	}
	void svm_run_constructors(void);
	svm_run_constructors();
	extern uint32_t svm_xip_delta;
	printf("ScummVM: heap %lu KB, XIP delta 0x%08lx\n", (unsigned long)(heap_total() / 1024), (unsigned long)svm_xip_delta);

	find_game(ACTIVE_FILE ? ACTIVE_FILE->path : "");
	rg_storage_mkdir(SAVE_DIR);
	logf_("ScummVM: game folder %s\n", g_game);
	cache_game();
	find_keys();
	logf_("ScummVM: %d files, %d in external flash\n", g_files, g_mapped);
	if (!g_files) {
		message3("Game files not found in", g_game, "(see README / scummvm.log)");
		plat_quit();
	}

	audio_start();
	/* with game=: "scummvm -p <folder> [--platform=] [--language=] <game id>" (the game ScummVM
	   would add by hand); without it: --auto-detect, the first game found in the folder */
	const char *argv[12];
	int argc = 0;
	argv[argc++] = "scummvm";
	argv[argc++] = "-p";
	argv[argc++] = g_game;
	if (g_opt_platform[0]) argv[argc++] = g_opt_platform;
	if (g_opt_language[0]) argv[argc++] = g_opt_language;
	if (g_opt_extra[0]) {                    /* under the game's folder, unless absolute */
		if (g_opt_extra[0] == '/') snprintf(g_arg_extra, sizeof g_arg_extra, "--extrapath=%s", g_opt_extra);
		else snprintf(g_arg_extra, sizeof g_arg_extra, "--extrapath=%s/%s", g_game, g_opt_extra);
		if (is_dir(g_arg_extra + 12)) {
			argv[argc++] = g_arg_extra;
			logf_("ScummVM: %s\n", g_arg_extra);
		} else
			logf_("ScummVM: extrapath: no folder %s, left out\n", g_arg_extra + 12);
	}
	extern uint8_t svm_load_state;
	extern int8_t svm_save_slot;
	if (g_bs && bs_zone_save_pending()) {
		/* Broken Sword saved itself on its way into a zone that is in the flash now
		   (gw_sword1_zone): that save, newer than any other, whatever was chosen */
		argv[argc++] = "-x"; argv[argc++] = "98";
	} else {
		if (svm_load_state) odroid_system_emu_load_state(svm_save_slot);   /* -> rg_load() */
		/* "resume" in the launcher: the firmware's load callback ran already (svm_main's caller
		   asked for it), ScummVM loads that file as its slot 99 as soon as the game starts */
		if (gw_rg_load_at_start()) { argv[argc++] = "-x"; argv[argc++] = "99"; }
	}
	argv[argc++] = g_opt_game[0] ? g_opt_game : "--auto-detect";
	argv[argc] = NULL;
	for (uint32_t i = 0; i < STACK_SIZE / 4; i++) g_stack[i] = STACK_PAINT;
	int res = run_on_stack(argc, argv, gw_scummvm_main, (uint8_t *)g_stack + STACK_SIZE);
	char tail[64];
	last_log_line(tail, sizeof tail);
	if (res || strstr(tail, "Error running game")) {
		/* ScummVM gave up (no game detected...): say so instead of just leaving */
		char last[64];
		last_log_line(last, sizeof last);
		message3("ScummVM could not start the game:", last, g_game);
	}
	plat_quit();
	return 0;
}
