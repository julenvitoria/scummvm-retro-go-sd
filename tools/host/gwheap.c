/*
 * The console's heap on the PC: linked into build/host/scummvm_host_gwheap (make
 * host-gwheap), it runs the PC build inside src/heap.c with pools of a fixed size, to see
 * where a game runs out of memory. (Not as an LD_PRELOAD: the program's own definitions of
 * heap_free_bytes()/heap_can_alloc() would win over the library's.)
 *
 *   GW_WAV=/dev/null GW_HEAP_BIG=800 GW_HEAP_SMALL=260 GW_HEAP_SCREEN=136 build/host/scummvm_host_gwheap <game>
 *   (GW_WAV: with no sound output ScummVM would not load the music, and use less memory)
 *
 * Sizes in KB. The PC has 64-bit pointers, so the same program needs roughly 1.2-1.3x the
 * console's memory: scale the console's figures (scummvm.log prints them) accordingly.
 * Out of memory prints "heap: out of memory..." and aborts like on the console.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <execinfo.h>
#include <stdint.h>

#define HEAP_LAZY_INIT
#include "../../src/heap.c"

/* plat_log (platform_host.c) prints a backtrace of an allocation that fails */

/* GW_HEAP_TRACE=<bytes>: every allocation at least that big, with who asked and what is used.
   GW_HEAP_PEAK=<file>: what was live at the heap's peak, one line per block ("size pc pc..."
   as offsets in the executable, for addr2line): tools/host/heap_peak.py sums it by caller. */
extern char __executable_start;
#define LIVE_BITS 18
#define DEPTH 8
typedef struct { void *p; uint32_t n; uintptr_t pc[DEPTH]; } live_t;
static live_t g_live[1 << LIVE_BITS], *g_snap;
static uint32_t g_snap_n, g_snap_used, g_last_snap;
static int g_peak_on = -1;
/* the snapshot lives outside the traced heap */
static live_t *malloc_shim_snapshot(void) { return mmap(0, sizeof(live_t) << LIVE_BITS, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); }

static uint32_t live_slot(void *p)
{
	uint32_t h = (uint32_t)(((uintptr_t)p >> 3) * 2654435761u) >> (32 - LIVE_BITS);
	while (g_live[h].p && g_live[h].p != p) h = (h + 1) & ((1 << LIVE_BITS) - 1);
	return h;
}

extern int heap_host_bypass;

static void peak_dump(void)
{
	heap_host_bypass = 1;                /* fopen's buffer: not from a heap that may be full */
	FILE *f = fopen(getenv("GW_HEAP_PEAK"), "w");
	heap_host_bypass = 0;
	if (!f || !g_snap) return;
	fprintf(f, "# peak %u KB\n", g_snap_used / 1024);
	for (uint32_t i = 0; i < g_snap_n; i++) {
		fprintf(f, "%u", g_snap[i].n);
		for (int k = 0; k < DEPTH; k++) fprintf(f, " 0x%lx", (unsigned long)(g_snap[i].pc[k] - (uintptr_t)&__executable_start));
		fputc('\n', f);
	}
	fclose(f);
}

/* the first failure: dump what is live then (the run may well crash before exit) */
void heap_walk(void (*cb)(void *ptr, size_t size, int used, void *user), void *user);
static void map_cb(void *ptr, size_t size, int used, void *user)
{
	FILE *f = (FILE *)user;
	uint32_t h = live_slot(ptr);
	fprintf(f, "%p %7zu %s", ptr, size, used ? "used" : "FREE");
	if (used && g_live[h].p == ptr)
		for (int k = 0; k < 3; k++) fprintf(f, " 0x%lx", (unsigned long)(g_live[h].pc[k] - (uintptr_t)&__executable_start));
	fputc('\n', f);
}

void heap_host_oom(void)
{
	static int once;
	if (g_peak_on > 0 && !once++) {
		peak_dump();
		const char *m = getenv("GW_HEAP_MAP");
		if (m) {
			heap_host_bypass = 1;
			FILE *f = fopen(m, "w");
			heap_host_bypass = 0;
			if (f) { heap_walk(map_cb, f); fclose(f); }
		}
	}
}

static int g_busy;                       /* backtrace() allocates too */

void heap_trace(size_t n, void *p)
{
	static long min = -1;
	if (min < 0) min = getenv("GW_HEAP_TRACE") ? atol(getenv("GW_HEAP_TRACE")) : 0;
	if (g_peak_on < 0) {
		g_peak_on = getenv("GW_HEAP_PEAK") != NULL;
		if (g_peak_on) atexit(peak_dump);
	}
	if (g_busy) return;
	g_busy = 1;
	if (g_peak_on) {
		void *bt[DEPTH + 3];
		int k = backtrace(bt, DEPTH + 3);
		live_t *e = &g_live[live_slot(p)];
		e->p = p; e->n = (uint32_t)n;
		for (int i = 0; i < DEPTH; i++) e->pc[i] = i + 3 < k ? (uintptr_t)bt[i + 3] : 0;
		uint32_t used = heap_used() + (uint32_t)n;
		if (used > g_last_snap + 4096) {         /* a new peak: keep what is live now */
			g_last_snap = used;
			if (!g_snap) g_snap = malloc_shim_snapshot();
			g_snap_n = 0;
			for (uint32_t i = 0; i < (1u << LIVE_BITS); i++)
				if (g_live[i].p) g_snap[g_snap_n++] = g_live[i];
			g_snap_used = used;
		}
	}
	if (min && (long)n >= min) {
		char msg[96];
		snprintf(msg, sizeof msg, "heap: big %zu at %p (used %u KB)\n", n, p, (unsigned)(heap_used() / 1024));
		fputs(msg, stdout);
		fflush(stdout);
		void *bt[12];
		backtrace_symbols_fd(bt, backtrace(bt, 12), 1);
	}
	g_busy = 0;
}

void heap_untrace(void *p)
{
	if (g_peak_on <= 0) return;
	uint32_t h = live_slot(p);
	if (g_live[h].p != p) return;
	/* delete with backward shift (linear probing) */
	uint32_t i = h, j = h;
	for (;;) {
		j = (j + 1) & ((1 << LIVE_BITS) - 1);
		if (!g_live[j].p) break;
		uint32_t k = (uint32_t)(((uintptr_t)g_live[j].p >> 3) * 2654435761u) >> (32 - LIVE_BITS);
		if ((i <= j) ? (i < k && k <= j) : (i < k || k <= j)) continue;
		g_live[i] = g_live[j];
		i = j;
	}
	g_live[i].p = NULL;
}

/* opendir() wants a 32 KB buffer; FatFs on the console does not: kept out of the measured heap */
void *heap_host_alloc(size_t n)
{
	size_t *q = mmap(0, n + 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (q == MAP_FAILED) return NULL;
	q[0] = 0x6877686f7374ull; q[1] = n + 16;
	return q + 2;
}
int heap_host_free(void *p)
{
	size_t *q = (size_t *)p - 2;
	if (((uintptr_t)p & 4095) != 16 || q[0] != 0x6877686f7374ull) return 0;
	munmap(q, q[1]);
	return 1;
}

void heap_lazy_init(void)
{
	static int done;
	if (done++) return;
	const char *b = getenv("GW_HEAP_BIG"), *s = getenv("GW_HEAP_SMALL");
	size_t big = (b ? atoi(b) : 800) * 1024, small = (s ? atoi(s) : 260) * 1024;
	void *pb = mmap(0, big, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	void *ps = mmap(0, small, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	heap_add_to(pb, big, 0);
	heap_add_to(ps, small, 1);
	const char *sc = getenv("GW_HEAP_SCREEN");
	if (sc && atoi(sc)) {
		size_t screen = atoi(sc) * 1024;
		heap_add_to(mmap(0, screen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), screen, 2);
	}
}

int posix_memalign(void **out, size_t align, size_t n)
{
	void *p = memalign(align, n);
	if (!p) return ENOMEM;
	*out = p;
	return 0;
}
void *aligned_alloc(size_t align, size_t n) { return memalign(align, n); }
void *valloc(size_t n) { return memalign(4096, n); }
void *pvalloc(size_t n) { return memalign(4096, (n + 4095) & ~(size_t)4095); }
