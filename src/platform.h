/*
 * The small C interface between the ScummVM backend (gw_system.cpp) and the machine it runs
 * on: platform_core.c talks to the Retro-Go firmware, platform_host.c to a PC (tests,
 * screenshots, scripted input). Everything the backend needs and nothing else.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- lifetime ---- */
void     plat_log(const char *msg);               /* one line or a fragment, as ScummVM logs */
void     plat_quit(void) __attribute__((noreturn)); /* back to the launcher */
void     plat_fatal(void) __attribute__((noreturn));/* ScummVM's error(): say why, then back to the launcher */
const char *plat_game_path(void);                 /* folder of the game to run */
const char *plat_save_path(void);                 /* folder for saves and scummvm.ini */

/* ---- time ---- */
uint32_t plat_millis(void);
void     plat_delay(uint32_t ms);                 /* also keeps the firmware alive */
void     plat_date(int *year, int *mon, int *mday, int *hour, int *min, int *sec, int *wday);

/* ---- screen: 8-bit indexed ---- */
void     plat_set_palette(const uint8_t *rgb, int start, int count);
/* show a w x h frame (pitch bytes per line); the cursor is already drawn into it */
void     plat_redraw(void);                         /* the last frame again (the on-screen keyboard changed) */
void     plat_present(const uint8_t *pixels, int pitch, int w, int h);

/* ---- input ---- */
enum {
	PLAT_UP = 1 << 0, PLAT_DOWN = 1 << 1, PLAT_LEFT = 1 << 2, PLAT_RIGHT = 1 << 3,
	PLAT_A = 1 << 4, PLAT_B = 1 << 5, PLAT_GAME = 1 << 6, PLAT_TIME = 1 << 7,
	PLAT_SELECT = 1 << 8, PLAT_START = 1 << 9
};
uint32_t plat_buttons(void);                      /* PLAT_* held right now */

/* ---- sound: mono, 16-bit, at plat_audio_rate() ---- */
uint32_t plat_audio_rate(void);                   /* 0: no sound */
int      plat_audio_free(void);                   /* samples that can be written right now */
void     plat_audio_write(const int16_t *s, int n);
/* fn is called while the platform waits (plat_delay) and on every plat_present/plat_buttons:
   the backend tops the sound buffer up from there (there are no threads) */
void     plat_set_pump(void (*fn)(void));

/* ---- files ---- */
typedef struct {
	bool exists, is_dir;
	uint32_t size;
} plat_stat_t;
plat_stat_t plat_stat(const char *path);
/* calls cb(name, is_dir, size, arg) for each entry; false if the folder cannot be read */
bool     plat_list_dir(const char *path, void (*cb)(const char *name, bool is_dir, uint32_t size, void *arg), void *arg);
/* the whole file readable in memory (external flash on the console), or NULL to stream it.
   *key (may be NULL): the byte the copy in memory is XORed with against the file on the card --
   SCUMM v4-v6 data files are stored DECRYPTED (key 0x69), so the engine can use its resources
   where they lie instead of copying them into the heap (patches/scummvm-slim.patch) */
const uint8_t *plat_map_file(const char *path, uint32_t *size, uint8_t *key);

/* Is this the start of a SCUMM data file XORed with 0x69 (v5/v6 "LECF" bundle, v4 "LE" disk)? */
static inline int svm_xor69_data(const uint8_t *p, uint32_t n)
{
	if (n < 8) return 0;
	if ((p[0] ^ 0x69) == 'L' && (p[1] ^ 0x69) == 'E' && (p[2] ^ 0x69) == 'C' && (p[3] ^ 0x69) == 'F') return 1;
	return (p[4] ^ 0x69) == 'L' && (p[5] ^ 0x69) == 'E';
}
/* ...and the start of one already decrypted? ("LECF" is also a v7 file that never was encrypted:
   that one counts only when the game's index is encrypted, the caller's 'xor_index') */
static inline int svm_plain_data(const uint8_t *p, uint32_t n, int xor_index)
{
	if (n < 8) return 0;
	if (p[0] == 'L' && p[1] == 'E' && p[2] == 'C' && p[3] == 'F') return xor_index;
	return p[4] == 'L' && p[5] == 'E';
}
/* the v5/v6 index ("RNAM"/"MAXS" XORed with 0x69) */
static inline int svm_xor69_index(const uint8_t *p, uint32_t n)
{
	if (n < 4) return 0;
	uint8_t a = p[0] ^ 0x69, b = p[1] ^ 0x69, c = p[2] ^ 0x69, d = p[3] ^ 0x69;
	return (a == 'R' && b == 'N' && c == 'A' && d == 'M') || (a == 'M' && b == 'A' && c == 'X' && d == 'S');
}
bool     plat_mkdir(const char *path);

void    *plat_fopen(const char *path, const char *mode);
uint32_t plat_fread(void *buf, uint32_t n, void *f);
uint32_t plat_fwrite(const void *buf, uint32_t n, void *f);
bool     plat_fseek(void *f, int32_t off, int whence);
int32_t  plat_ftell(void *f);
void     plat_fclose(void *f);

#ifdef __cplusplus
}
#endif
