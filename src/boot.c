/*
 * Resident boot of the ScummVM core (RAM image, never in external flash).
 *
 * The firmware loads the RAM image and jumps to gnw_core_entry (sdk gw_core_entry.S), which
 * branches here (GNW_CORE_ENTRY_TARGET). Before anything in the XIP image may run:
 *
 *  1. find our own .bin in /cores (the footer xip_pack.py appended carries a build id),
 *  2. have the firmware cache the whole .bin in external flash; while it programs, the
 *     relocate callback adds (where the image landed - where it was linked) to every word of
 *     the XIP image that points into it (bitmap XIP_RELOCS),
 *  3. do the same for the RAM image's words that point into XIP (bitmap RAM_RELOCS),
 *  4. the core's main (platform_core.c), which sets the heap up and only then calls
 *     svm_run_constructors(): ScummVM's static constructors allocate.
 *
 * On a later launch the firmware finds the .bin already cached (same path, size and date):
 * the callback does not run, the image is already relocated for that same address.
 *
 * While the firmware programs the flash, the flash is not mapped: nothing here may call into
 * the XIP image, and nothing of the C library outside what the linker script keeps in RAM.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "gw_core_bridge.h"
#include "svm_xip.h"

extern uint8_t __RAM_EMU_START__[], __RAM_EMU_END__[];   /* __CORE_CODE_END__ / __CORE_BSS_END__: gw_core_bridge.h */
extern void (*__svm_init_array_start[])(void), (*__svm_init_array_end[])(void);

/* firmware, via the bridge (objcopy --redefine-syms on this object) */
uint8_t *odroid_overlay_cache_file_in_flash_relocate(const char *file_path, uint32_t *file_size_p, bool byte_swap,
                                                     void (*cb)(uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t));
void odroid_system_switch_app(int app);
void wdog_refresh(void);
uint32_t ram_get_free_size(void);
void *ram_malloc(size_t size);

char svm_core_path[160];
static svm_xip_footer_t g_foot;
static uint8_t *g_xip_bits;              /* XIP_RELOCS bitmap, read before caching */
static uint32_t g_delta;
uint32_t svm_xip_delta;                 /* XIP address = linked address + this (for addr2line) */
int svm_boot_error;                     /* 0 ok; see svm_boot() */

/* ---- 1. which .bin is ours ---- */

static bool read_footer(const char *path, svm_xip_footer_t *f)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return false;
	bool ok = fseek(fp, -(long)sizeof *f, SEEK_END) == 0 && fread(f, 1, sizeof *f, fp) == sizeof *f &&
	          f->magic == SVM_XIP_MAGIC && f->build_id == SVM_BUILD_ID;
	fclose(fp);
	return ok;
}

#include "ff.h"
static bool find_core(void)
{
	DIR d;
	FILINFO fi;
	bool found = false;
	if (f_opendir(&d, "/cores") != FR_OK) return false;
	while (!found && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
		if (fi.fattrib & AM_DIR) continue;
		size_t n = strlen(fi.fname);
		if (n < 5 || (strcmp(fi.fname + n - 4, ".bin") && strcmp(fi.fname + n - 4, ".BIN"))) continue;
		if (n + 8 > sizeof svm_core_path) continue;
		strcpy(svm_core_path, "/cores/");     /* no snprintf: it lives in the XIP image */
		strcat(svm_core_path, fi.fname);
		found = read_footer(svm_core_path, &g_foot);
	}
	f_closedir(&d);
	return found;
}

/* ---- 2. relocate the XIP image while it is programmed ---- */

static void relocate_cb(uint8_t *buf, uint32_t len, uint32_t offset_in_file, uint8_t *file_address, uint32_t file_size)
{
	(void)file_size;
	uint32_t img0 = g_foot.xip_offset, img1 = g_foot.xip_offset + g_foot.xip_size;
	uint32_t delta = (uint32_t)file_address + g_foot.xip_offset - g_foot.xip_vma;
	/* words are 4-aligned in the file (the image starts on a 4 KB boundary) */
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
	/* a relocated word that straddles two buffers cannot happen: both buffers and words
	   are 4-aligned (circular_flash_write reads in whole blocks) */
}

static bool read_range(void *dst, uint32_t off, uint32_t n)
{
	FILE *fp = fopen(svm_core_path, "rb");
	if (!fp) return false;
	bool ok = fseek(fp, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, fp) == n;
	fclose(fp);
	return ok;
}

/* ---- the boot ---- */

int svm_main(void);                     /* platform_core.c */

int svm_boot(void)
{
	if (!find_core()) { svm_boot_error = 1; return svm_main(); }

	/* the bitmaps go to the top of what is free; the heap does not exist yet */
	uint32_t xip_bytes = (g_foot.xip_size / 4 + 7) / 8, ram_bytes = g_foot.ram_relocs_size;
	uint8_t *tmp = (uint8_t *)(((uintptr_t)&__CORE_BSS_END__ + 7) & ~7u);
	if (tmp + xip_bytes + ram_bytes > __RAM_EMU_END__ - 16 * 1024) { svm_boot_error = 2; return svm_main(); }
	g_xip_bits = tmp;
	uint8_t *ram_bits = tmp + xip_bytes;
	if (!read_range(g_xip_bits, g_foot.xip_relocs_offset, xip_bytes) ||
	    !read_range(ram_bits, g_foot.ram_relocs_offset, ram_bytes)) { svm_boot_error = 3; return svm_main(); }

	uint32_t size = 0;
	uint8_t *file = odroid_overlay_cache_file_in_flash_relocate(svm_core_path, &size, false, relocate_cb);
	if (!file || size < g_foot.xip_offset + g_foot.xip_size) { svm_boot_error = 4; return svm_main(); }
	g_delta = (uint32_t)file + g_foot.xip_offset - g_foot.xip_vma;
	svm_xip_delta = g_delta;

	/* 3. the RAM image's pointers into XIP (bitmap over __RAM_EMU_START__ .. __CORE_CODE_END__) */
	uint32_t *ram = (uint32_t *)__RAM_EMU_START__;
	uint32_t words = ((uintptr_t)&__CORE_CODE_END__ - (uintptr_t)__RAM_EMU_START__) / 4;
	for (uint32_t w = 0; w < words; w++)
		if (ram_bits[w >> 3] & (1u << (w & 7))) ram[w] += g_delta;
	/* the RAM image is code too: drop what the caches hold of it */
	__asm volatile("dsb");
	for (uint32_t a = (uintptr_t)__RAM_EMU_START__ & ~31u; a < (uintptr_t)&__CORE_CODE_END__; a += 32)
		*(volatile uint32_t *)0xE000EF68 = a;          /* DCCMVAC */
	__asm volatile("dsb");
	*(volatile uint32_t *)0xE000EF50 = 0;              /* ICIALLU */
	__asm volatile("dsb\n isb");

	return svm_main();
}

/* C++ constructors: their code is in XIP and they allocate, so svm_main() calls this once the
   XIP image is mapped and the heap exists */
void svm_run_constructors(void)
{
	for (void (**f)(void) = __svm_init_array_start; f < __svm_init_array_end; f++) (*f)();
}

/* entry target (GNW_CORE_ENTRY_TARGET) */
int app_main_scummvm(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
	(void)load_state; (void)start_paused; (void)save_slot;
	return svm_boot();
}

/* ---- newlib glue ---- */

/* malloc & co. are heap.c (TLSF over several RAM regions): nothing grows with sbrk */
void *_sbrk(ptrdiff_t incr) { (void)incr; errno = ENOMEM; return (void *)-1; }

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _read(int fd, char *buf, int n) { (void)fd; (void)buf; (void)n; return 0; }
int _write(int fd, const char *buf, int n) { (void)fd; printf("%.*s", n, buf); return n; }   /* firmware log */
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int code) { (void)code; for (;;) odroid_system_switch_app(0); }
int _open(const char *p, int f, int m) { (void)p; (void)f; (void)m; return -1; }
int _gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return -1; }
int _unlink(const char *p) { (void)p; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; return -1; }
void _init(void) {}
void _fini(void) {}
void *__dso_handle = 0;                 /* static destructors are never run: leaving reboots */
