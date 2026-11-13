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

#ifndef BIG_REQUEST
#define BIG_REQUEST (8 * 1024)
#endif

enum { HEAP_BIG, HEAP_SMALL, HEAP_SCREEN, HEAPS };

/* Set around the allocation of the SCUMM engine's screen buffers (patches/scummvm-slim.patch):
   they are freed and made again at every room change, and in their own heap they always find
   their hole again -- in the shared one, resources loaded meanwhile split it (seen on the PC
   with the console's heap size: a 64 KB screen failing with 240 KB free). */
int heap_screen_hint;
int heap_screen_spill;
#define MAX_POOLS 7

static tlsf_t g_tlsf[HEAPS];
static struct { uintptr_t lo, hi; int heap; pool_t pool; } g_pool[MAX_POOLS];
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
		g_pool[g_pools].pool = tlsf_get_pool(g_tlsf[kind]);
		g_total += bytes - tlsf_size() - tlsf_pool_overhead();
	} else {
		g_pool[g_pools].pool = tlsf_add_pool(g_tlsf[kind], (void *)a, bytes);
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
void heap_trace(size_t n, void *p);      /* tools/host/gwheap.c: GW_HEAP_TRACE, GW_HEAP_PEAK */
void heap_untrace(void *p);
void *heap_host_alloc(size_t n);         /* host-only allocations outside the console heap */
int heap_host_free(void *p);
void heap_host_oom(void);
int heap_host_bypass;                    /* set around host libc calls with no console counterpart (opendir) */
#else
static inline void heap_lazy_init(void) {}
#endif

#ifdef HEAP_MAP_LOG
/* (the default; "make core HEAP_MAP_LOG=" leaves it out) the blocks of 2 KB or more and who asked for them, and the free holes,
   written to scummvm.log at the first failure (a heap map on the console itself) */
#define MAP_N 192
static struct { void *p; uint32_t n; void *caller; } g_map[MAP_N];
static void map_add(void *p, uint32_t n, void *caller)
{
	if (n < 2048) return;
	for (int i = 0; i < MAP_N; i++) if (!g_map[i].p) { g_map[i].p = p; g_map[i].n = n; g_map[i].caller = caller; return; }
}
static void map_del(void *p) { for (int i = 0; i < MAP_N; i++) if (g_map[i].p == p) { g_map[i].p = NULL; return; } }
static void hole_cb(void *ptr, size_t size, int used, void *user)
{
	(void)user;
	if (!used && size >= 2048) { static char m[64]; snprintf(m, sizeof m, "heap: free %p %lu\n", ptr, (unsigned long)size); plat_log(m); }
}
static void map_dump(void)
{
	static int once;
	if (once++) return;
	for (int i = 0; i < MAP_N; i++)
		if (g_map[i].p) { static char m[64]; snprintf(m, sizeof m, "heap: block %p %lu from %p\n", g_map[i].p, (unsigned long)g_map[i].n, g_map[i].caller); plat_log(m); }
	for (int i = 0; i < g_pools; i++) if (g_pool[i].pool) tlsf_walk_pool(g_pool[i].pool, hole_cb, NULL);
}
#else
static inline void map_add(void *p, uint32_t n, void *caller) { (void)p; (void)n; (void)caller; }
static inline void map_del(void *p) { (void)p; }
static inline void map_dump(void) {}
#endif

/* Set by the engine (patches/scummvm-slim.patch: the SCUMM resource cache): frees something
   it can do without and returns nonzero, or returns 0 when there is nothing left to free.
   Asked before an allocation is given up. */
int (*heap_low_memory_hook)(uint32_t n);

static void *alloc(size_t align, size_t n, void *caller)
{
	if (!g_pools) heap_lazy_init();
#ifdef HEAP_LAZY_INIT
	if (heap_host_bypass) return heap_host_alloc(n);
#endif
	int first = n >= BIG_REQUEST ? HEAP_BIG : HEAP_SMALL;
	/* last, what the screen buffers leave of their heap -- with SCUMM v7 only
	   (heap_screen_spill), whose screens are made once */
	int order[3] = { first, !first, heap_screen_spill ? HEAP_SCREEN : -1 };
	if (heap_screen_hint) { order[0] = HEAP_SCREEN; order[1] = first; order[2] = !first; }
	void *p = NULL;
	static int in_hook;
	for (;;) {
		for (int k = 0; k < 3 && !p; k++) {
			tlsf_t t = order[k] >= 0 ? g_tlsf[order[k]] : NULL;
			if (t) p = align ? tlsf_memalign(t, align, n) : tlsf_malloc(t, n);
		}
		if (p || !heap_low_memory_hook || in_hook) break;
		in_hook = 1;
		int freed = heap_low_memory_hook((uint32_t)n);
		in_hook = 0;
		if (!freed) break;
	}
#ifdef HEAP_TRACE_SCREEN
	if (heap_screen_hint) { static char m[96]; snprintf(m, sizeof m, "heap: screen %lu -> %p\n", (unsigned long)n, p); plat_log(m); }
#endif
	if (!p) {
		errno = ENOMEM;
		if (g_pools) {
#ifdef HEAP_LAZY_INIT
			heap_host_oom();
#endif
			/* to the firmware log and to scummvm.log (plat_log, platform_core.c) */
			static char msg[112];
			snprintf(msg, sizeof msg, "heap: out of memory for %lu bytes (used %lu of %lu KB, peak %lu KB) from %p\n",
			         (unsigned long)n, (unsigned long)(g_used / 1024), (unsigned long)(g_total / 1024),
			         (unsigned long)(g_peak / 1024), caller);
			plat_log(msg);
			map_dump();
		}
	}
	if (p) map_add(p, (uint32_t)n, caller);
#ifdef HEAP_LAZY_INIT
	if (p) heap_trace(n, p);
#endif
	return track(p);
}

void *malloc(size_t n) { return alloc(0, n ? n : 1, __builtin_return_address(0)); }
/* src/gw_new.cpp: operator new, with its own caller for the log */
void *heap_malloc_from(size_t n, void *caller) { return alloc(0, n ? n : 1, caller); }

void free(void *p)
{
	if (!p) return;
	tlsf_t t = owner(p);
#ifdef HEAP_LAZY_INIT
	if (!t) { heap_host_free(p); return; }
	heap_untrace(p);
#else
	if (!t) return;
#endif
	map_del(p);
	g_used -= tlsf_block_size(p);
	tlsf_free(t, p);
}

void *calloc(size_t a, size_t b)
{
	size_t n = a * b;
	if (b && n / b != a) return NULL;
	void *p = heap_malloc_from(n, __builtin_return_address(0));
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
		map_del(p);
		map_add(q, (uint32_t)n, __builtin_return_address(0));
#ifdef HEAP_LAZY_INIT
		heap_untrace(p);
		heap_trace(n, q);
#endif
		return track(q);
	}
	/* no room in its own heap: move it to the other one */
	q = malloc(n);
	if (!q) return NULL;
	memcpy(q, p, old < n ? old : n);
	free(p);
	return q;
}

void *memalign(size_t align, size_t n) { return alloc(align, n ? n : 1, __builtin_return_address(0)); }
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

#ifdef HEAP_LAZY_INIT
/* tools/host/gwheap.c: every block of every pool, for a map of the heap */
void heap_walk(void (*cb)(void *ptr, size_t size, int used, void *user), void *user)
{
	for (int i = 0; i < g_pools; i++)
		if (g_pool[i].pool) tlsf_walk_pool(g_pool[i].pool, cb, user);
}
#endif

/* free bytes in both heaps (not one block: what the SCUMM resource cache measures itself against) */
uint32_t heap_free_bytes(void) { return g_total - g_used; }
