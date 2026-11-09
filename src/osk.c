/*
 * On-screen keyboard, in the style of the DosXT/DosAT cores: four rows of keys in the
 * 20-line bands above and below the 320x200 picture (two rows in each), so the game is never
 * covered or moved.
 *
 *   GAME opens and closes it, the D-pad moves, A types, B closes.
 *   Shf latches for the next key (twice: locked).
 *
 * The game owns all 256 colours of the LUT8 CLUT, so the keyboard is drawn with the
 * palette entries nearest to its four colours, looked up at every draw.
 */
#include <stdint.h>
#include <string.h>
#include "osk.h"
#include "font_cp437.h"

/* ScummVM's key codes (common/keyboard.h): printable keys are their lowercase ASCII */
enum { K_BACKSPACE = 8, K_RETURN = 13, K_ESCAPE = 27, K_SPACE = 32, K_F1 = 282 };
enum { A_F1 = 315 };                     /* Common::ASCII_F1, F2 = 316... */
#define K_SHIFT (-1)
#define FKEY(n) { "F" #n, K_F1 + (n) - 1, A_F1 + (n) - 1 }

typedef struct { const char *label; int keycode, ascii; } key_t;

static const key_t r0[] = { { "Esc", K_ESCAPE, 27 }, { "1", '1', '1' }, { "2", '2', '2' }, { "3", '3', '3' },
	{ "4", '4', '4' }, { "5", '5', '5' }, { "6", '6', '6' }, { "7", '7', '7' }, { "8", '8', '8' }, { "9", '9', '9' },
	{ "0", '0', '0' }, { "-", '-', '-' }, { "\x1B-", K_BACKSPACE, 8 }, FKEY(1), FKEY(2), FKEY(3), FKEY(4), FKEY(5),
	FKEY(6), { 0, 0, 0 } };
static const key_t r1[] = { { "Q", 'q', 'q' }, { "W", 'w', 'w' }, { "E", 'e', 'e' }, { "R", 'r', 'r' },
	{ "T", 't', 't' }, { "Y", 'y', 'y' }, { "U", 'u', 'u' }, { "I", 'i', 'i' }, { "O", 'o', 'o' }, { "P", 'p', 'p' },
	{ "Ent", K_RETURN, 13 }, FKEY(7), FKEY(8), FKEY(9), { "F10", K_F1 + 9, A_F1 + 9 }, { 0, 0, 0 } };
static const key_t r2[] = { { "Shf", K_SHIFT, 0 }, { "A", 'a', 'a' }, { "S", 's', 's' }, { "D", 'd', 'd' },
	{ "F", 'f', 'f' }, { "G", 'g', 'g' }, { "H", 'h', 'h' }, { "J", 'j', 'j' }, { "K", 'k', 'k' }, { "L", 'l', 'l' },
	{ "'", '\'', '\'' }, { "!", '!', '!' }, { "?", '?', '?' }, { ":", ':', ':' }, { 0, 0, 0 } };
static const key_t r3[] = { { "Z", 'z', 'z' }, { "X", 'x', 'x' }, { "C", 'c', 'c' }, { "V", 'v', 'v' },
	{ "B", 'b', 'b' }, { "N", 'n', 'n' }, { "M", 'm', 'm' }, { ",", ',', ',' }, { ".", '.', '.' },
	{ "Spc", K_SPACE, ' ' }, { "Ent", K_RETURN, 13 }, { 0, 0, 0 } };

#define ROWS 4
static const key_t *const rows[ROWS] = { r0, r1, r2, r3 };
static const int row_y[ROWS] = { 1, 10, 221, 230 };     /* bands: lines 0..19 and 220..239 */

static int g_open, g_row = 0, g_col = 0, g_shift;       /* g_shift: 0 off, 1 next key, 2 locked */

static int row_len(int r) { int n = 0; while (rows[r][n].label) n++; return n; }
static int key_w(const key_t *k) { return (int)strlen(k->label) * 8 + 4; }
static int key_x(int r, int c) { int x = 0; for (int i = 0; i < c; i++) x += key_w(&rows[r][i]); return x; }

int osk_is_open(void) { return g_open; }
void osk_open(void) { g_open = 1; }
void osk_close(void) { g_open = 0; }

static void move_row(int dr)
{
	int cx = key_x(g_row, g_col) + key_w(&rows[g_row][g_col]) / 2;
	g_row = (g_row + dr + ROWS) % ROWS;
	int best = 0, bestd = 1 << 30;
	for (int c = 0; c < row_len(g_row); c++) {
		int m = key_x(g_row, c) + key_w(&rows[g_row][c]) / 2;
		int d = m > cx ? m - cx : cx - m;
		if (d < bestd) { bestd = d; best = c; }
	}
	g_col = best;
}

int osk_input(uint32_t pressed, uint32_t up, uint32_t down, uint32_t left, uint32_t right, uint32_t type,
              uint32_t close, osk_emit_fn emit, void *arg)
{
	if (!g_open) return 0;
	if (pressed & close) { g_open = 0; return 1; }
	if (pressed & up) { move_row(-1); return 1; }
	if (pressed & down) { move_row(1); return 1; }
	if (pressed & left) { g_col = (g_col + row_len(g_row) - 1) % row_len(g_row); return 1; }
	if (pressed & right) { g_col = (g_col + 1) % row_len(g_row); return 1; }
	if (pressed & type) {
		const key_t *k = &rows[g_row][g_col];
		if (k->keycode == K_SHIFT) { g_shift = (g_shift + 1) % 3; return 1; }
		int ascii = k->ascii, shift = g_shift != 0;
		if (shift && ascii >= 'a' && ascii <= 'z') ascii -= 'a' - 'A';
		emit(k->keycode, ascii, shift, arg);
		if (g_shift == 1) g_shift = 0;
		return 1;
	}
	return 0;
}

/* ---- drawing ---- */

static uint8_t nearest(const uint32_t *clut, uint32_t rgb)
{
	int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255, best = 0, bestd = 1 << 30;
	for (int i = 0; i < 256; i++) {
		int dr = (int)((clut[i] >> 16) & 255) - r, dg = (int)((clut[i] >> 8) & 255) - g, db = (int)(clut[i] & 255) - b;
		int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < bestd) { bestd = d; best = i; }
	}
	return (uint8_t)best;
}

static void fill(uint8_t *fb, int x, int y, int w, int h, uint8_t c)
{
	for (int j = 0; j < h; j++)
		if (y + j >= 0 && y + j < 240) {
			int x0 = x < 0 ? 0 : x, x1 = x + w > 320 ? 320 : x + w;
			if (x1 > x0) memset(fb + (y + j) * 320 + x0, c, x1 - x0);
		}
}

static void text(uint8_t *fb, int x, int y, const char *s, uint8_t fg)
{
	for (; *s; s++, x += 8)
		for (int j = 0; j < 8; j++) {
			uint8_t bits = font_cp437[(uint8_t)*s][j];
			for (int i = 0; i < 8; i++)
				if ((bits & (0x80 >> i)) && x + i < 320) fb[(y + j) * 320 + x + i] = fg;
		}
}

void osk_draw(uint8_t *fb, const uint32_t *clut)
{
	if (!g_open) return;
	uint8_t black = nearest(clut, 0x000000), key = nearest(clut, 0x404050), fg = nearest(clut, 0xFFFFFF),
	        sel = nearest(clut, 0xFFE040), latch = nearest(clut, 0x30A040);
	fill(fb, 0, 0, 320, 20, black);
	fill(fb, 0, 220, 320, 20, black);
	for (int r = 0; r < ROWS; r++) {
		int x = 0;
		for (int c = 0; rows[r][c].label; c++) {
			const key_t *k = &rows[r][c];
			int w = key_w(k);
			uint8_t bg = key, f = fg;
			if (k->keycode == K_SHIFT && g_shift) bg = latch;
			if (r == g_row && c == g_col) { bg = sel; f = black; }
			fill(fb, x, row_y[r], w - 1, 9, bg);
			text(fb, x + 2, row_y[r] + 1, k->label, f);
			x += w;
		}
	}
}
