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
