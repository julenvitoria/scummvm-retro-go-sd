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
#define HEAP_LAZY_INIT
#include "../../src/heap.c"

/* plat_log (platform_host.c) prints a backtrace of an allocation that fails */

/* GW_HEAP_TRACE=<bytes>: every allocation at least that big, with who asked and what is used */
void heap_trace(size_t n, void *p)
{
	static long min = -1;
	if (min < 0) min = getenv("GW_HEAP_TRACE") ? atol(getenv("GW_HEAP_TRACE")) : 0;
	static int busy;                       /* backtrace() allocates too */
	if (!min || (long)n < min || busy) return;
	busy = 1;
	char msg[96];
	snprintf(msg, sizeof msg, "heap: big %zu at %p (used %u KB)\n", n, p, (unsigned)(heap_used() / 1024));
	fputs(msg, stdout);
	fflush(stdout);
	void *bt[12];
	backtrace_symbols_fd(bt, backtrace(bt, 12), 1);
	busy = 0;
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
