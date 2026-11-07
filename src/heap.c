/*
 * The heap of the ScummVM core: TLSF (src/third_party/tlsf, BSD) over every piece of RAM the
 * firmware leaves to a core, replacing newlib's malloc (which wants one contiguous sbrk):
 *
 *   RAM_EMU after our image   ~640 KB   (up to the reserve kept for the firmware's ram_malloc)
 *   LCD bonus pool            ~150 KB   (exists in LUT8 mode: 2 x 8-bit framebuffers instead of 16-bit)
 *   DTCM core pool            ~90 KB    (what dtc_malloc() has, minus a margin before the stack)
 *
 * The firmware's own AHB heap is left alone: its menus allocate there (an exhausted AHB heap is
 * what broke the Retro-Go menu in Cannonball).
 *
 * Resident (RAM image). Newlib's internal calls (_malloc_r...) land here too.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <reent.h>
#include <stdio.h>
#include "third_party/tlsf/tlsf.h"

static tlsf_t g_tlsf;
static uint32_t g_total, g_used, g_peak;

void heap_add(void *mem, size_t bytes)
{
	uintptr_t a = ((uintptr_t)mem + 7) & ~(uintptr_t)7;
	bytes = (bytes - (a - (uintptr_t)mem)) & ~(size_t)7;
	if (bytes < tlsf_size() + tlsf_pool_overhead() + 1024) return;
	if (!g_tlsf) {
		g_tlsf = tlsf_create_with_pool((void *)a, bytes);
		g_total += bytes - tlsf_size() - tlsf_pool_overhead();
	} else {
		tlsf_add_pool(g_tlsf, (void *)a, bytes);
		g_total += bytes - tlsf_pool_overhead();
	}
}

uint32_t heap_total(void) { return g_total; }
uint32_t heap_used(void) { return g_used; }
uint32_t heap_peak(void) { return g_peak; }

static void *track(void *p)
{
	if (p) {
		g_used += tlsf_block_size(p);
		if (g_used > g_peak) g_peak = g_used;
	}
	return p;
}

void *malloc(size_t n)
{
	if (!g_tlsf) { errno = ENOMEM; return NULL; }
	void *p = track(tlsf_malloc(g_tlsf, n ? n : 1));
	if (!p) {
		errno = ENOMEM;
		printf("heap: out of memory for %lu bytes (used %lu of %lu KB, peak %lu KB)\n", (unsigned long)n,
		       (unsigned long)(g_used / 1024), (unsigned long)(g_total / 1024), (unsigned long)(g_peak / 1024));
	}
	return p;
}

void free(void *p)
{
	if (!p) return;
	g_used -= tlsf_block_size(p);
	tlsf_free(g_tlsf, p);
}

void *calloc(size_t a, size_t b)
{
	size_t n = a * b;
	if (b && n / b != a) return NULL;
	void *p = malloc(n);
	if (p) memset(p, 0, n);
	return p;
}

void *realloc(void *p, size_t n)
{
	if (!p) return malloc(n);
	if (!n) { free(p); return NULL; }
	size_t old = tlsf_block_size(p);
	void *q = tlsf_realloc(g_tlsf, p, n);
	if (!q) { errno = ENOMEM; return NULL; }
	g_used -= old;
	return track(q);
}

void *memalign(size_t align, size_t n) { return g_tlsf ? track(tlsf_memalign(g_tlsf, align, n)) : NULL; }
size_t malloc_usable_size(void *p) { return p ? tlsf_block_size(p) : 0; }

void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void *_memalign_r(struct _reent *r, size_t a, size_t n) { (void)r; return memalign(a, n); }
