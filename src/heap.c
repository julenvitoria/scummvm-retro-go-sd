/*
 * The heap of the ScummVM core: TLSF (src/third_party/tlsf, BSD) over every piece of RAM the
 * firmware leaves to a core, replacing newlib's malloc (which wants one contiguous sbrk):
 *
 *   RAM_EMU after our image   ~640 KB   (up to the reserve kept for the firmware's ram_malloc)
 *   LCD bonus pool            ~150 KB   (exists in LUT8 mode: 2 x 8-bit framebuffers instead of 16-bit)
 *   DTCM core pool            ~40 KB    (what dtc_malloc() has after ScummVM's stack, minus a margin)
 *
 * Two TLSF heaps, to keep the big region in one piece: ScummVM asks for several 64 KB blocks
 * (screens, text surface) while it also makes thousands of small allocations; mixed in one
 * heap, the small ones split the free space until no 64 KB hole is left (seen in gwemu: a
 * 64 KB request failing with 210 KB free). Small requests go to the SMALL heap first (bonus
 * pool + DTCM), big ones to the BIG heap (RAM_EMU); each falls back to the other.
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
#ifdef __NEWLIB__
#include <reent.h>
#endif
#include <stdio.h>
#include "third_party/tlsf/tlsf.h"

void plat_log(const char *msg);

#define BIG_REQUEST (8 * 1024)

enum { HEAP_BIG, HEAP_SMALL, HEAP_SCREEN, HEAPS };

/* Set around the allocation of the SCUMM engine's screen buffers (patches/scummvm-slim.patch):
   they are freed and made again at every room change, and in their own heap they always find
   their hole again -- in the shared one, resources loaded meanwhile split it (seen on the PC
   with the console's heap size: a 64 KB screen failing with 240 KB free). */
int heap_screen_hint;
#define MAX_POOLS 6

static tlsf_t g_tlsf[HEAPS];
static struct { uintptr_t lo, hi; int heap; } g_pool[MAX_POOLS];
static int g_pools;
static uint32_t g_total, g_used, g_peak;

/* kind: 0 = big heap, 1 = small heap, 2 = screen buffers */
void heap_add_to(void *mem, size_t bytes, int kind)
{
	uintptr_t a = ((uintptr_t)mem + 7) & ~(uintptr_t)7;
	bytes = (bytes - (a - (uintptr_t)mem)) & ~(size_t)7;
	if (!mem || bytes < tlsf_size() + tlsf_pool_overhead() + 1024 || g_pools >= MAX_POOLS) {
		static char msg[80];
		snprintf(msg, sizeof msg, "heap: pool %p (%lu bytes) not used\n", mem, (unsigned long)bytes);
		plat_log(msg);
		return;
	}
	if (!g_tlsf[kind]) {
		g_tlsf[kind] = tlsf_create_with_pool((void *)a, bytes);
		g_total += bytes - tlsf_size() - tlsf_pool_overhead();
	} else {
		tlsf_add_pool(g_tlsf[kind], (void *)a, bytes);
		g_total += bytes - tlsf_pool_overhead();
	}
	g_pool[g_pools].lo = a;
	g_pool[g_pools].hi = a + bytes;
	g_pool[g_pools].heap = kind;
	g_pools++;
}

void heap_add(void *mem, size_t bytes) { heap_add_to(mem, bytes, HEAP_BIG); }

uint32_t heap_total(void) { return g_total; }
uint32_t heap_used(void) { return g_used; }
uint32_t heap_peak(void) { return g_peak; }

static tlsf_t owner(void *p)
{
	for (int i = 0; i < g_pools; i++)
		if ((uintptr_t)p >= g_pool[i].lo && (uintptr_t)p < g_pool[i].hi) return g_tlsf[g_pool[i].heap];
	return NULL;
}

static void *track(void *p)
{
	if (p) {
		g_used += tlsf_block_size(p);
		if (g_used > g_peak) g_peak = g_used;
	}
	return p;
}

/* the PC test shim (tools/host/gwheap.c) creates its pools on the first allocation */
#ifdef HEAP_LAZY_INIT
void heap_lazy_init(void);
#else
static inline void heap_lazy_init(void) {}
#endif

static void *alloc(size_t align, size_t n)
{
	if (!g_pools) heap_lazy_init();
	int first = n >= BIG_REQUEST ? HEAP_BIG : HEAP_SMALL;
	int order[3] = { first, !first, -1 };
	if (heap_screen_hint) { order[0] = HEAP_SCREEN; order[1] = first; order[2] = !first; }
	void *p = NULL;
	for (int k = 0; k < 3 && !p; k++) {
		tlsf_t t = order[k] >= 0 ? g_tlsf[order[k]] : NULL;
		if (t) p = align ? tlsf_memalign(t, align, n) : tlsf_malloc(t, n);
	}
	if (!p) {
		errno = ENOMEM;
		if (g_pools) {
			/* to the firmware log and to scummvm.log (plat_log, platform_core.c) */
			static char msg[112];
			snprintf(msg, sizeof msg, "heap: out of memory for %lu bytes (used %lu of %lu KB, peak %lu KB)\n",
			         (unsigned long)n, (unsigned long)(g_used / 1024), (unsigned long)(g_total / 1024),
			         (unsigned long)(g_peak / 1024));
			plat_log(msg);
		}
	}
	return track(p);
}

void *malloc(size_t n) { return alloc(0, n ? n : 1); }

void free(void *p)
{
	if (!p) return;
	tlsf_t t = owner(p);
	if (!t) return;
	g_used -= tlsf_block_size(p);
	tlsf_free(t, p);
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
	tlsf_t t = owner(p);
	size_t old = tlsf_block_size(p);
	void *q = t ? tlsf_realloc(t, p, n) : NULL;
	if (q) {
		g_used -= old;
		return track(q);
	}
	/* no room in its own heap: move it to the other one */
	q = malloc(n);
	if (!q) return NULL;
	memcpy(q, p, old < n ? old : n);
	free(p);
	return q;
}

void *memalign(size_t align, size_t n) { return alloc(align, n ? n : 1); }
size_t malloc_usable_size(void *p) { return p ? tlsf_block_size(p) : 0; }

#ifdef __NEWLIB__
void *_malloc_r(struct _reent *r, size_t n) { (void)r; return malloc(n); }
void _free_r(struct _reent *r, void *p) { (void)r; free(p); }
void *_calloc_r(struct _reent *r, size_t a, size_t b) { (void)r; return calloc(a, b); }
void *_realloc_r(struct _reent *r, void *p, size_t n) { (void)r; return realloc(p, n); }
void *_memalign_r(struct _reent *r, size_t a, size_t n) { (void)r; return memalign(a, n); }
#endif

/* would an allocation of n bytes succeed right now (in one piece)? */
int heap_can_alloc(uint32_t n)
{
	int first = n >= BIG_REQUEST ? HEAP_BIG : HEAP_SMALL;
	int order[3] = { heap_screen_hint ? HEAP_SCREEN : -1, first, !first };
	for (int k = 0; k < 3; k++) {
		tlsf_t t = order[k] >= 0 ? g_tlsf[order[k]] : NULL;
		void *p = t ? tlsf_malloc(t, n) : NULL;
		if (p) { tlsf_free(t, p); return 1; }
	}
	return 0;
}

/* free bytes in both heaps (not one block: what the SCUMM resource cache measures itself against) */
uint32_t heap_free_bytes(void) { return g_total - g_used; }
