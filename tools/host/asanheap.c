/* The PC test build with AddressSanitizer: the console heap's entry points on the PC's malloc,
   so that ASan sees every block (a use after free, a write past the end). Linked instead of
   gwheap.c by "make host-asan". */
#include <stdint.h>
#include <stdlib.h>
void *heap_malloc_from(size_t n, void *caller) { (void)caller; return malloc(n ? n : 1); }
int heap_check(void) { return 0; }
int (*heap_low_memory_hook)(uint32_t n);
