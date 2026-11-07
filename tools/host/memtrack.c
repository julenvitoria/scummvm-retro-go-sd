#define _GNU_SOURCE
#include <dlfcn.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <execinfo.h>
/* LD_PRELOAD heap meter: live bytes, peak, and a timeline every 250 ms to $MEMTRACK_OUT */
static void *(*r_malloc)(size_t); static void (*r_free)(void*); static void *(*r_realloc)(void*,size_t);
static void *(*r_calloc)(size_t,size_t);
static volatile long live, peak, nallocs, big; static int inited;
static char boot[65536]; static size_t bootp;
static void init(void){ if(inited) return; inited=1;
  r_malloc=dlsym(RTLD_NEXT,"malloc"); r_free=dlsym(RTLD_NEXT,"free"); r_realloc=dlsym(RTLD_NEXT,"realloc"); r_calloc=dlsym(RTLD_NEXT,"calloc"); }
static FILE *bigf; static long bigmin=8192; static void add(void*p){ if(!p) return; long s=malloc_usable_size(p); if(s>=bigmin && bigf){ void *bt[10]; int n=backtrace(bt,10); fprintf(bigf,"A %p %ld",p,s); for(int i=2;i<n;i++) fprintf(bigf," %p",bt[i]); fputc(10,bigf); fflush(bigf);} __sync_add_and_fetch(&live,s); __sync_add_and_fetch(&nallocs,1); if(live>peak) peak=live; }
static void sub(void*p){ if(!p) return; if((char*)p>=boot && (char*)p<boot+sizeof boot) return; long s=malloc_usable_size(p); if(s>=bigmin && bigf) {fprintf(bigf,"F %p %ld\n",p,s); fflush(bigf);} __sync_sub_and_fetch(&live,s); }
void *malloc(size_t n){ if(!r_malloc){ if(inited){ void*p=boot+bootp; bootp+=(n+15)&~15; return p;} init(); } void*p=r_malloc(n); add(p); return p; }
void free(void*p){ if(!p) return; if((char*)p>=boot && (char*)p<boot+sizeof boot) return; if(!r_free) init(); sub(p); r_free(p); }
void *calloc(size_t a,size_t b){ if(!r_calloc){ void*p=boot+bootp; bootp+=(a*b+15)&~15; memset(p,0,a*b); return p;} void*p=r_calloc(a,b); add(p); return p; }
void *realloc(void*o,size_t n){ if(!r_realloc) init(); if(o && (char*)o>=boot && (char*)o<boot+sizeof boot){ void*p=malloc(n); memcpy(p,o,n); return p;} sub(o); void*p=r_realloc(o,n); add(p); return p; }
static void *ticker(void*a){ FILE*f=fopen(getenv("MEMTRACK_OUT")?getenv("MEMTRACK_OUT"):"/dev/stderr","w"); struct timespec t0,t; clock_gettime(CLOCK_MONOTONIC,&t0);
  for(;;){ usleep(250000); clock_gettime(CLOCK_MONOTONIC,&t); fprintf(f,"%.2f live %ld peak %ld n %ld\n",(t.tv_sec-t0.tv_sec)+(t.tv_nsec-t0.tv_nsec)/1e9,live,peak,nallocs); fflush(f);} return 0; }
__attribute__((constructor)) static void start(void){ init(); if(getenv("MEMTRACK_MIN")) bigmin=atol(getenv("MEMTRACK_MIN")); if(getenv("MEMTRACK_BIG")){ static char vb[1<<20]; void *b[2]; backtrace(b,2); FILE*f=fopen(getenv("MEMTRACK_BIG"),"w"); setvbuf(f,vb,_IOFBF,sizeof vb); { FILE*m=fopen("/proc/self/maps","r"); char ln[512]; if(m && fgets(ln,sizeof ln,m)) fprintf(f,"B %s",ln); if(m) fclose(m);} bigf=f; } pthread_t th; pthread_create(&th,0,ticker,0); }
