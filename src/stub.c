/*
 * The loader: the scummvm.bin the firmware knows (one ScummVM tab) holds only this. ScummVM
 * itself comes in one image per engine, /cores/scummvm/<engine>.svm -- a whole ScummVM with
 * that engine alone -- so a game's memory and flash carry no other engine's tables or code.
 *
 *  1. which engine: from the files in the game's folder and the table engines.txt (the
 *     .scummvm file may say "engine=sky" to force one),
 *  2. the image's XIP part is cached in external flash and relocated to where it landed
 *     (as boot.c did for the single .bin), its RAM part read to where it was linked, above
 *     this loader,
 *  3. jump to its entry (boot.c, svm_engine_entry). Nothing of the loader runs after that:
 *     the engine adds this memory to its heap.
 *
 * No printf-family and nothing of ScummVM here: this is all the RAM the loader may take.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "gw_lcd.h"
#include "gw_malloc.h"
#include "rom_manager.h"
#include "appid.h"
#include "rg_storage.h"
#include "gw_core_bridge.h"
#include "svm_xip.h"
#include "gw_core_i18n.h"
#include "ff.h"

void wdog_refresh(void);

extern uint8_t __RAM_EMU_START__[], __RAM_EMU_END__[];
#define RAM_TOP ((uintptr_t)__RAM_EMU_END__)

uint8_t *odroid_overlay_cache_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p, bool byte_swap,
                                                     void (*cb)(uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t));

#define GAMES_DIR "/scummvm"
#define RESERVE   (16 * 1024)             /* RAM_EMU's top, the firmware's ram_malloc() (platform_core.c) */

static char g_path[96];                   /* the engine image */
static char g_game[200];                  /* the game's folder */
static char g_wanted[200];                /* the folder the .scummvm file asks for */
static char g_engine[16];
static svm_eng_footer_t g_foot;
static uint8_t *g_xip_bits;

/* ---- which engine ---- */

static bool exists(const char *dir, const char *name)
{
	char p[280];
	if (strlen(dir) + strlen(name) + 2 > sizeof p) return false;
	strcpy(p, dir);
	strcat(p, "/");
	strcat(p, name);
	return rg_storage_stat(p).exists;
}

static bool is_dir(const char *p)
{
	rg_stat_t s = rg_storage_stat(p);
	return s.exists && s.is_dir;
}

static bool same_key(const char *line, const char *key)
{
	size_t n = strlen(key);
	for (size_t i = 0; i < n; i++) {
		char c = line[i];
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c != key[i]) return false;
	}
	while (line[n] == ' ' || line[n] == '\t') n++;
	return line[n] == '=';
}

/* the .scummvm file, as platform_core.c's find_game() reads it: "key=value" lines are options,
   the first other line is the folder */
static bool find_game(const char *picked)
{
	char text[200] = "";
	FILE *f = fopen(picked, "rb");
	if (f) {
		size_t n = fread(text, 1, sizeof text - 1, f), k = 0;
		fclose(f);
		bool utf16 = n >= 2 && (((uint8_t)text[0] == 0xFF && (uint8_t)text[1] == 0xFE) ||
		                        ((uint8_t)text[0] == 0xFE && (uint8_t)text[1] == 0xFF));
		for (size_t i = utf16 ? 2 : 0; i < n; i++)
			if (text[i]) text[k++] = text[i];
		text[k] = 0;
	}
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
			if (same_key(line, "engine")) {
				char *v = eq + 1;
				while (*v == ' ' || *v == '\t') v++;
				size_t k = 0;
				for (; v[k] && k < sizeof g_engine - 1; k++)
					g_engine[k] = v[k] >= 'A' && v[k] <= 'Z' ? v[k] + 32 : v[k];
				g_engine[k] = 0;
			}
		} else if (!have_folder && *line) {
			t = line;
			have_folder = true;
		}
		line = after;
	}
	g_game[0] = 0;
	if (*t && strlen(t) + sizeof GAMES_DIR + 2 < sizeof g_game) {
		if (*t == '/') strcpy(g_game, t);
		else { strcpy(g_game, GAMES_DIR "/"); strcat(g_game, t); }
		strcpy(g_wanted, g_game);
		if (is_dir(g_game)) return true;
	}
	const char *base = strrchr(picked, '/');
	base = base ? base + 1 : picked;
	if (strlen(base) + sizeof GAMES_DIR + 2 < sizeof g_game) {
		strcpy(g_game, GAMES_DIR "/");
		strcat(g_game, base);
		char *dot = strrchr(g_game, '.');
		if (dot && dot > g_game + sizeof GAMES_DIR) *dot = 0;
		if (!g_wanted[0]) strcpy(g_wanted, g_game);
		if (is_dir(g_game)) return true;
	}
	if (strlen(picked) < sizeof g_game) {
		strcpy(g_game, picked);                        /* the first layout */
		char *slash = strrchr(g_game, '/');
		if (slash) *slash = 0;
	}
	return false;
}

/* The table of engines, SVM_ENG_DIR/engines.txt (config/engines.txt): "file = engine" lines,
   the first that matches a file of the game's folder wins. It names engines there is no
   image for as well, so that their games are told apart from files nobody knows. */
#define TABLE SVM_ENG_DIR "/engines.txt"
enum { ENG_OK, ENG_NO_TABLE, ENG_EMPTY, ENG_UNKNOWN };
static char g_table[2048];

static char low(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool same(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (low(a[i]) != low(b[i])) return false;
	return true;
}
/* a name against a pattern with at most one '*' */
static bool match(const char *pat, size_t plen, const char *name)
{
	const char *star = memchr(pat, '*', plen);
	size_t n = strlen(name);
	if (!star) return n == plen && same(pat, name, plen);
	size_t pre = (size_t)(star - pat), suf = plen - pre - 1;
	return n >= pre + suf && same(pat, name, pre) && same(star + 1, name + n - suf, suf);
}

/* line 'i' of the table: its pattern and its engine; false past the last line */
static bool table_line(int want, const char **pat, size_t *plen, const char **eng, size_t *elen)
{
	const char *p = g_table;
	for (int i = 0; *p; ) {
		const char *end = p + strcspn(p, "\r\n");
		const char *eq = memchr(p, '=', (size_t)(end - p));
		while (*p == ' ' || *p == '\t') p++;
		if (eq && *p != '#') {
			const char *pe = eq, *v = eq + 1, *ve = end;
			while (pe > p && (pe[-1] == ' ' || pe[-1] == '\t')) pe--;
			while (v < ve && (*v == ' ' || *v == '\t')) v++;
			while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
			if (pe > p && ve > v && i++ == want) {
				*pat = p; *plen = (size_t)(pe - p); *eng = v; *elen = (size_t)(ve - v);
				return true;
			}
		}
		p = end + strspn(end, "\r\n");
	}
	return false;
}

static int engine_of(const char *dir)
{
	FILE *f = fopen(TABLE, "rb");
	if (!f) return ENG_NO_TABLE;
	g_table[fread(g_table, 1, sizeof g_table - 1, f)] = 0;
	fclose(f);

	const char *pat, *eng;
	size_t plen, elen;
	int best = -1, entries = 0;
	DIR d;
	FILINFO fi;
	if (f_opendir(&d, dir) != FR_OK) return ENG_EMPTY;
	while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
		wdog_refresh();
		entries++;
		for (int i = 0; (best < 0 || i < best) && table_line(i, &pat, &plen, &eng, &elen); i++)
			if (match(pat, plen, fi.fname)) { best = i; break; }
	}
	f_closedir(&d);
	/* a file in a subfolder: looked up by its name */
	for (int i = 0; (best < 0 || i < best) && table_line(i, &pat, &plen, &eng, &elen); i++) {
		char name[40];
		if (!memchr(pat, '/', plen) || plen >= sizeof name) continue;
		memcpy(name, pat, plen);
		name[plen] = 0;
		if (exists(dir, name)) { best = i; break; }
	}
	if (best < 0) return entries ? ENG_UNKNOWN : ENG_EMPTY;
	table_line(best, &pat, &plen, &eng, &elen);
	if (elen >= sizeof g_engine) elen = sizeof g_engine - 1;
	for (size_t i = 0; i < elen; i++) g_engine[i] = low(eng[i]);
	g_engine[elen] = 0;
	return ENG_OK;
}

/* ---- the image ---- */

static bool read_range(void *dst, uint32_t off, uint32_t n)
{
	FILE *fp = fopen(g_path, "rb");
	if (!fp) return false;
	bool ok = fseek(fp, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, fp) == n;
	fclose(fp);
	return ok;
}

static bool read_footer(void)
{
	FILE *fp = fopen(g_path, "rb");
	if (!fp) return false;
	bool ok = fseek(fp, -(long)sizeof g_foot, SEEK_END) == 0 && fread(&g_foot, 1, sizeof g_foot, fp) == sizeof g_foot &&
	          g_foot.magic == SVM_ENG_MAGIC && g_foot.build_id == SVM_BUILD_ID;
	fclose(fp);
	return ok;
}

static void relocate_cb(uint8_t *buf, uint32_t len, uint32_t offset_in_file, uint8_t *file_address, uint32_t file_size)
{
	(void)file_size;
	uint32_t img0 = g_foot.xip_offset, img1 = g_foot.xip_offset + g_foot.xip_size;
	uint32_t delta = (uint32_t)file_address + g_foot.xip_offset - g_foot.xip_vma;
	uint32_t from = offset_in_file > img0 ? offset_in_file : img0;
	uint32_t to = offset_in_file + len < img1 ? offset_in_file + len : img1;
	for (uint32_t off = (from + 3) & ~3u; off + 4 <= to; off += 4) {
		uint32_t w = (off - img0) >> 2;
		if (g_xip_bits[w >> 3] & (1u << (w & 7))) {
			uint8_t *p = buf + (off - offset_in_file);
			uint32_t v;
			memcpy(&v, p, 4);
			v += delta;
			memcpy(p, &v, 4);
		}
	}
}

/* ---- failing: a notice, and back to the launcher ---- */

static void repaint(void) {}

#define TR(en, es, fr, de, it, pt) { { "en", en }, { "es", es }, { "fr", fr }, { "de", de }, { "it", it }, { "pt", pt }, GW_I18N_END }
static const gw_i18n_entry_t
	t_nofolder[] = TR("Game folder not found or empty:", "Carpeta del juego vacia o no encontrada:",
	                  "Dossier du jeu introuvable ou vide :", "Spielordner fehlt oder ist leer:",
	                  "Cartella del gioco assente o vuota:", "Pasta do jogo vazia ou nao encontrada:"),
	t_unknown[] = TR("No known game in this folder:", "Ningun juego conocido en esta carpeta:",
	                 "Aucun jeu connu dans ce dossier :", "Kein bekanntes Spiel in diesem Ordner:",
	                 "Nessun gioco noto in questa cartella:", "Nenhum jogo conhecido nesta pasta:"),
	t_noengine1[] = TR("This game's engine is not in this", "El motor de este juego no esta en",
	                   "Le moteur de ce jeu n'est pas dans", "Die Engine dieses Spiels fehlt in",
	                   "Il motore di questo gioco non e in", "O motor deste jogo nao esta nesta"),
	t_noengine2[] = TR("version of the core:", "esta version del core:", "cette version du core :",
	                   "dieser Version des Cores:", "questa versione del core:", "versao do core:"),
	t_files1[] = TR("Core files missing or from another", "Faltan ficheros del core o son de otra",
	                "Fichiers du core absents ou d'une autre", "Core-Dateien fehlen oder sind aus einer",
	                "File del core assenti o di un'altra", "Faltam ficheiros do core ou sao de outra"),
	t_files2[] = TR("release. Copy them all again:", "version. Copialos todos otra vez:",
	                "version. Recopiez-les tous :", "anderen Version. Alle neu kopieren:",
	                "versione. Copiali tutti di nuovo:", "versao. Copia-os todos outra vez:"),
	t_flash[] = TR("Not enough room in external flash:", "No hay sitio en la flash externa:",
	               "Pas assez de place dans la flash :", "Zu wenig Platz im externen Flash:",
	               "Spazio insufficiente nella flash:", "Sem espaco na flash externa:");

static void fail2(const char *why, const char *why2, const char *what) __attribute__((noreturn));
static void fail2(const char *why, const char *why2, const char *what)
{
	odroid_system_init(APPID_CORE, 22050);
	odroid_system_emu_init(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	lcd_setup_framebuffers(LCD_MODE_LUT8);
	lcd_clear_buffers();
	odroid_dialog_choice_t opts[4], last = ODROID_DIALOG_CHOICE_LAST;
	const char *t[3] = { why, why2, what };
	int n = 0;
	for (int i = 0; i < 3; i++)
		if (t[i] && t[i][0]) {
			odroid_dialog_choice_t o = { n, t[i], "", 1, NULL };
			opts[n++] = o;
		}
	opts[n] = last;
	printf("ScummVM loader: %s %s %s\n", why, why2 ? why2 : "", what);
	odroid_overlay_dialog("ScummVM", opts, 0, &repaint, 0);
	odroid_system_switch_app(0);
	for (;;) {}
}
static void fail(const char *why) __attribute__((noreturn));
static void fail(const char *why) { fail2(why, NULL, g_path[0] ? g_path : SVM_ENG_DIR); }

int app_main_scummvm(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
	(void)start_paused;
	/* the firmware's ram_malloc() would hand out the memory right after this loader, where
	   the engine goes: it gets the top of RAM_EMU, as while a game runs */
	ram_start = (uint32_t)(RAM_TOP - RESERVE);

	bool folder = find_game(ACTIVE_FILE ? ACTIVE_FILE->path : "");
	if (!g_engine[0])                                  /* no "engine=" in the .scummvm file */
		switch (engine_of(g_game)) {
		case ENG_NO_TABLE: fail2(gw_i18n(t_files1), gw_i18n(t_files2), TABLE);
		case ENG_EMPTY:    fail2(gw_i18n(t_nofolder), NULL, folder ? g_game : g_wanted);
		case ENG_UNKNOWN:
			if (!folder) fail2(gw_i18n(t_nofolder), NULL, g_wanted);
			fail2(gw_i18n(t_unknown), NULL, g_game);
		}
	strcpy(g_path, SVM_ENG_DIR "/");
	strcat(g_path, g_engine);
	strcat(g_path, ".svm");
	printf("ScummVM loader: %s -> %s\n", g_game, g_path);
	if (!rg_storage_stat(g_path).exists) fail2(gw_i18n(t_noengine1), gw_i18n(t_noengine2), g_path);
	if (!read_footer()) fail2(gw_i18n(t_files1), gw_i18n(t_files2), g_path);
	if (g_foot.ram_vma < (uint32_t)(uintptr_t)&__CORE_BSS_END__ ||
	    g_foot.bss_end > (uint32_t)(RAM_TOP - RESERVE - 64 * 1024))
		fail("Engine file does not fit this loader:");

	/* the bitmaps go under the firmware's reserve, far above the image's RAM part */
	uint32_t xip_bytes = (g_foot.xip_size / 4 + 7) / 8, ram_bytes = g_foot.ram_relocs_size;
	uint8_t *ram_bits = (uint8_t *)(RAM_TOP - RESERVE - ((ram_bytes + 7) & ~7u));
	g_xip_bits = ram_bits - ((xip_bytes + 7) & ~7u);
	if ((uint32_t)(uintptr_t)g_xip_bits < g_foot.bss_end) fail("Not enough RAM to start:");
	if (!read_range(g_xip_bits, g_foot.xip_relocs_offset, xip_bytes) ||
	    !read_range(ram_bits, g_foot.ram_relocs_offset, ram_bytes)) fail("Cannot read the engine file:");

	uint32_t size = 0;
	uint8_t *file = odroid_overlay_cache_file_in_flash_relocate(g_path, &size, false, relocate_cb);
	if (!file || size < g_foot.xip_offset + g_foot.xip_size) fail(gw_i18n(t_flash));
	uint32_t delta = (uint32_t)file + g_foot.xip_offset - g_foot.xip_vma;

	/* the RAM part: read where it was linked, its pointers into XIP moved, its .bss zeroed */
	uint8_t *ram0 = (uint8_t *)(uintptr_t)g_foot.ram_vma;
	if (!read_range(ram0, 0, g_foot.ram_size)) fail("Cannot read the engine file:");
	uint32_t *ram = (uint32_t *)ram0;
	for (uint32_t w = 0; w < g_foot.ram_size / 4; w++)
		if (ram_bits[w >> 3] & (1u << (w & 7))) ram[w] += delta;
	memset(ram0 + g_foot.ram_size, 0, g_foot.bss_end - g_foot.ram_vma - g_foot.ram_size);
	/* it is code too: drop what the caches hold of it */
	__asm volatile("dsb");
	for (uint32_t a = g_foot.ram_vma & ~31u; a < g_foot.bss_end; a += 32)
		*(volatile uint32_t *)0xE000EF68 = a;          /* DCCMVAC */
	__asm volatile("dsb");
	*(volatile uint32_t *)0xE000EF50 = 0;              /* ICIALLU */
	__asm volatile("dsb\n isb");

	static svm_eng_boot_t boot;
	boot.load_state = load_state;
	boot.save_slot = save_slot;
	boot.xip_delta = delta;
	boot.path = g_path;
	boot.stub_start = __RAM_EMU_START__;
	boot.stub_end = (uint8_t *)(uintptr_t)g_foot.ram_vma;
	boot.image_id = g_foot.image_id;
	boot.loader_version = SVM_LOADER_VERSION;
	return ((int (*)(const svm_eng_boot_t *))(uintptr_t)g_foot.entry)(&boot);
}

/* ---- newlib glue (nothing here opens files through newlib or allocates) ---- */
void _exit(int code) { (void)code; odroid_system_switch_app(0); for (;;) {} }
void _init(void) {}
void _fini(void) {}
