/* Footer tools/xip_pack.py appends to the core .bin (last bytes of the file). */
#pragma once
#include <stdint.h>

#define SVM_XIP_MAGIC 0x50495853u        /* "SXIP" */
#ifndef SVM_BUILD_ID
#error "SVM_BUILD_ID must be defined (the Makefile passes one per build)"
#endif

typedef struct {
	uint32_t magic;
	uint32_t build_id;            /* this build's id: tells our .bin from another core's */
	uint32_t xip_vma;             /* where the XIP image was linked */
	uint32_t xip_offset;          /* file offset of the image (4 KB aligned) */
	uint32_t xip_size;
	uint32_t xip_relocs_offset;   /* bitmap, one bit per word of the image */
	uint32_t ram_relocs_offset;   /* bitmap, one bit per word of the RAM image */
	uint32_t ram_relocs_size;     /* bytes */
} svm_xip_footer_t;

/*
 * An engine image, /cores/scummvm/<engine>.svm (tools/xip_pack.py --engine): a whole ScummVM
 * with one engine, which the loader (src/stub.c, the scummvm.bin the firmware knows) picks
 * for the game, loads and starts. Layout: RAM image | pad to 4 KB | XIP image | XIP bitmap |
 * RAM bitmap | this footer. The RAM image is linked at SVM_ENG_RAM, above the loader.
 */
#define SVM_ENG_MAGIC 0x474E4553u        /* "SENG" */
#define SVM_ENG_RAM   (0x24000000u + 300u * 1024u + 12u * 1024u)   /* RAM_EMU + 12 KB (ld/scummvm_engine.ld) */
#define SVM_ENG_DIR   "/cores/scummvm"

typedef struct {
	uint32_t magic;
	uint32_t build_id;
	uint32_t xip_vma, xip_offset, xip_size;
	uint32_t xip_relocs_offset;
	uint32_t ram_relocs_offset, ram_relocs_size;
	uint32_t ram_vma;             /* = SVM_ENG_RAM */
	uint32_t ram_size;            /* bytes of the RAM image, at the start of the file */
	uint32_t bss_end;             /* the RAM image's .bss ends here (zeroed by the loader) */
	uint32_t entry;               /* svm_engine_entry, thumb bit set */
	uint32_t image_id;            /* CRC-32 of the file up to this footer: which build of the engine this is */
} svm_eng_footer_t;

#define SVM_LOADER_VERSION 2             /* src/stub.c; goes up when the loader changes */

/* what the loader hands the engine image */
typedef struct {
	uint8_t load_state;
	int8_t save_slot;
	uint32_t xip_delta;
	const char *path;             /* the engine image's file */
	uint8_t *stub_start, *stub_end;   /* the loader's own RAM: free once the engine runs */
	uint32_t image_id;            /* the footer's */
	uint32_t loader_version;      /* SVM_LOADER_VERSION */
} svm_eng_boot_t;
