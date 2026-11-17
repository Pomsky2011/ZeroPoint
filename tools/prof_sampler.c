/* A statistical profiler for when perf/ptrace are blocked (perf_event_paranoid, yama): an LD_PRELOAD library that
 * samples the program counter on SIGPROF and writes "<pc> <caller>" lines (module-relative hex) at exit; the caller is
 * the return address up the frame-pointer chain, so build the program with -fno-omit-frame-pointer to have it.
 *   cc -O2 -shared -fPIC -o prof_sampler.so tools/prof_sampler.c -ldl
 *   LD_PRELOAD=./prof_sampler.so PROF_OUT=samples.txt <program> ...
 *   tools/prof_report.py <program> samples.txt      (needs the program built with -g; -O3 -g is fine)
 * No per-call overhead (unlike gprof's -pg), and inlined code is attributed to its source line via addr2line -i.
 * Samples only the thread that receives SIGPROF (the one running the emulator in the headless tools). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ucontext.h>

#define MAX_SAMPLES (1 << 22)
static uintptr_t samples[MAX_SAMPLES][2];
static volatile unsigned count;
static uintptr_t stack_lo, stack_hi;   /* the main thread's stack: the only place a frame pointer is dereferenced */

static void on_prof(int sig, siginfo_t* info, void* ctx) {
    (void)sig; (void)info;
    if (count >= MAX_SAMPLES) return;
    const ucontext_t* uc = (const ucontext_t*)ctx;
    const uintptr_t* fp = (const uintptr_t*)uc->uc_mcontext.gregs[REG_RBP];
    samples[count][0] = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    /* RBP is only a frame pointer in code built with -fno-omit-frame-pointer (libc and SDL are not): read through it
     * only when it points into the main stack above the stack pointer. It may also be stale in a function's first or
     * last instructions: a few samples lie. */
    const uintptr_t rsp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    const int ok = (uintptr_t)fp >= rsp && (uintptr_t)fp + 16 <= stack_hi && rsp >= stack_lo && ((uintptr_t)fp & 7) == 0;
    samples[count][1] = ok ? fp[1] : 0;
    count++;
}

static void dump(void) {
    struct itimerval off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_PROF, &off, NULL);
    const char* path = getenv("PROF_OUT");
    FILE* f = fopen(path ? path : "prof_samples.txt", "w");
    if (!f) return;
    for (unsigned i = 0; i < count; i++) {
        Dl_info di;
        if (dladdr((void*)samples[i][0], &di) && di.dli_fbase)
            fprintf(f, "%lx %lx\n", (unsigned long)(samples[i][0] - (uintptr_t)di.dli_fbase),
                    (unsigned long)(samples[i][1] - (uintptr_t)di.dli_fbase));
    }
    fclose(f);
}

__attribute__((constructor)) static void start(void) {
    pthread_attr_t attr;
    void* addr = NULL;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
    }
    stack_lo = (uintptr_t)addr;
    stack_hi = (uintptr_t)addr + size;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    struct itimerval it = {{0, 1000}, {0, 1000}};   /* 1 kHz of CPU time */
    setitimer(ITIMER_PROF, &it, NULL);
    atexit(dump);
}
