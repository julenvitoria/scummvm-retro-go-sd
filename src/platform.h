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
const char *plat_game_path(void);                 /* folder of the game to run */
const char *plat_save_path(void);                 /* folder for saves and scummvm.ini */

/* ---- time ---- */
uint32_t plat_millis(void);
void     plat_delay(uint32_t ms);                 /* also keeps the firmware alive */
void     plat_date(int *year, int *mon, int *mday, int *hour, int *min, int *sec, int *wday);

/* ---- screen: 8-bit indexed ---- */
void     plat_set_palette(const uint8_t *rgb, int start, int count);
/* show a w x h frame (pitch bytes per line); the cursor is already drawn into it */
void     plat_present(const uint8_t *pixels, int pitch, int w, int h);

/* ---- input ---- */
enum {
	PLAT_UP = 1 << 0, PLAT_DOWN = 1 << 1, PLAT_LEFT = 1 << 2, PLAT_RIGHT = 1 << 3,
	PLAT_A = 1 << 4, PLAT_B = 1 << 5, PLAT_GAME = 1 << 6, PLAT_TIME = 1 << 7,
	PLAT_SELECT = 1 << 8, PLAT_START = 1 << 9
};
uint32_t plat_buttons(void);                      /* PLAT_* held right now */

/* ---- files ---- */
typedef struct {
	bool exists, is_dir;
	uint32_t size;
} plat_stat_t;
plat_stat_t plat_stat(const char *path);
/* calls cb(name, is_dir, size, arg) for each entry; false if the folder cannot be read */
bool     plat_list_dir(const char *path, void (*cb)(const char *name, bool is_dir, uint32_t size, void *arg), void *arg);
/* the whole file readable in memory (external flash on the console), or NULL to stream it */
const uint8_t *plat_map_file(const char *path, uint32_t *size);
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
