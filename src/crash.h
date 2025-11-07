#include <execinfo.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "ansicols.h"
#if defined(__aarch64__) || defined(__arm64__)
#  define ATOS_ARCH "arm64"
#elif defined(__x86_64__)
#  define ATOS_ARCH "x86_64"
#else
#  define ATOS_ARCH "arm64"  // shrug
#endif

static void crash_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)ucontext;

    void *frames[64];
    int n = backtrace(frames, 64);

    fprintf(stderr, COLOR_RED "\n=== CRASH === signal %d" COLOR_RESET, sig);
    if (sig == SIGSEGV) fprintf(stderr, " (SIGSEGV)");
    if (sig == SIGABRT) fprintf(stderr, " (SIGABRT)");
    if (sig == SIGTRAP) fprintf(stderr, " (SIGTRAP)");
    if (sig == SIGBUS)  fprintf(stderr, " (SIGBUS)");
    if (sig == SIGILL)  fprintf(stderr, " (SIGILL)");
    if (sig == SIGFPE)  fprintf(stderr, " (SIGFPE)");
    fprintf(stderr, ", addr=%p\n", info ? info->si_addr : NULL);

    backtrace_symbols_fd(frames, n, STDERR_FILENO);

    // collect unique interesting images (ginkgo, dsp.*.so, etc.)
    struct Image {
        const char   *path;
        uintptr_t     base;
    };
    struct Image imgs[16];
    int img_count = 0;

    for (int i = 0; i < n && img_count < (int)(sizeof(imgs)/sizeof(imgs[0])); ++i) {
        Dl_info dli;
        if (!dladdr(frames[i], &dli) || !dli.dli_fname || !dli.dli_fbase) continue;

        const char *p = dli.dli_fname;

        // skip obvious system libs; adjust if you like
        if (!strncmp(p, "/usr/lib/", 9)) continue;
        if (!strncmp(p, "/System/Library/", 16)) continue;
        if (!strncmp(p, "/Library/", 9)) continue;

        uintptr_t base = (uintptr_t)dli.dli_fbase;

        int found = 0;
        for (int j = 0; j < img_count; ++j)
            if (imgs[j].base == base) { found = 1; break; }

        if (!found) {
            imgs[img_count].path = p;
            imgs[img_count].base = base;
            ++img_count;
        }
    }
    fprintf(stderr, COLOR_GREEN "\n# atos commands: RUN THESE TO GET FILE/LINE INFO\n\n");
    // print an atos command per image
    for (int j = 0; j < img_count; ++j) {
        //fprintf(stderr, "\n# atos for %s\n", imgs[j].path);
        fprintf(stderr, COLOR_YELLOW "atos -o '%s' -arch " ATOS_ARCH " -l 0x%llx" COLOR_RESET,
                imgs[j].path, (unsigned long long)imgs[j].base);

        for (int i = 0; i < n; ++i) {
            Dl_info dli;
            if (dladdr(frames[i], &dli) && dli.dli_fbase &&
                (uintptr_t)dli.dli_fbase == imgs[j].base) {
                fprintf(stderr, COLOR_CYAN " 0x%llx",
                        (unsigned long long)(uintptr_t)frames[i]);
            }
        }
        fprintf(stderr, COLOR_RESET "\n" );
    }
    fprintf(stderr, COLOR_RESET "\n" );
    fflush(stderr);
    signal(sig, SIG_DFL);
    raise(sig);
}

void install_crash_handler(void) {
    struct sigaction sa;
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    sigaction(SIGSEGV, &sa, NULL); // invalid mem access
    sigaction(SIGABRT, &sa, NULL); // abort()
    sigaction(SIGBUS,  &sa, NULL); // misaligned access, etc.
    sigaction(SIGILL,  &sa, NULL); // illegal instruction
    sigaction(SIGTRAP, &sa, NULL); // __builtin_trap, breakpoints, etc.
    sigaction(SIGFPE,  &sa, NULL); // divide by zero, FP exc.
    
}

