/*
 * operator new / delete on the core's heap (src/heap.c). Two differences from libstdc++'s:
 * an allocation that fails returns NULL instead of throwing (the core is built with
 * -fno-exceptions -fcheck-new, so a throw was an abort; ScummVM checks for NULL in most
 * places), and the log line for it names the code that asked (heap.c prints the address).
 */
#include <new>
#include <stddef.h>
#include <stdlib.h>

extern "C" void *heap_malloc_from(size_t n, void *caller);

void *operator new(size_t n) { return heap_malloc_from(n, __builtin_return_address(0)); }
void *operator new[](size_t n) { return heap_malloc_from(n, __builtin_return_address(0)); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return heap_malloc_from(n, __builtin_return_address(0)); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return heap_malloc_from(n, __builtin_return_address(0)); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { free(p); }
