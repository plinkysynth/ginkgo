// clang -std=c11 -O2 host.c -o host
#define MINIAUDIO_IMPLEMENTATION
#include "3rdparty/miniaudio.h"
#include <stdatomic.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*dsp_fn_t)(void *G, float *o, const float *i, int frames);

static _Atomic(dsp_fn_t) g_dsp_req = NULL;  // current callback
static _Atomic(dsp_fn_t) g_dsp_used = NULL; // what version the main thread compiled
static void *G = NULL;                      // the state
static void *g_handle = NULL;
static int g_version = 0;

static void audio_cb(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    float *o = (float *)out;
    const float *i = (const float *)in;
    for (ma_uint32 k = 0; k < frames * 2; k++)
        o[k] = 0.f;
    dsp_fn_t dsp = atomic_load_explicit(&g_dsp_req, memory_order_acquire);
    if (!dsp)
        return;
    G = dsp(G, o, i, frames);
    atomic_store_explicit(&g_dsp_used, dsp, memory_order_release);
}

int64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

static bool kick_compile(void) {
    char cmd[1024];
    int version = g_version + 1;
    mkdir("build", 0755);
    snprintf(cmd, sizeof(cmd), "clang -std=c11 -O2 -fPIC  -dynamiclib -o build/dsp.%d.so dsp.c", version);
    int64_t t0 = get_time_us();
    int rc = system(cmd);
    int64_t t1 = get_time_us();
    if (!(rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0))
        return false;
    snprintf(cmd, sizeof(cmd), "build/dsp.%d.so", version);
    void *h = dlopen(cmd, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen %s failed: %s\n", cmd, dlerror());
        return false;
    }
    dsp_fn_t f = (dsp_fn_t)dlsym(h, "dsp");
    if (!f) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(h);
        return false;
    }
    atomic_store_explicit(&g_dsp_req, f, memory_order_release);
    while (g_handle != 0 && atomic_load_explicit(&g_dsp_used, memory_order_acquire) != f) {
        usleep(1000);
    }
    if (g_handle)
        dlclose(g_handle);
    g_handle = h;
    g_version = version;
    fprintf(stderr, "compile %s succeeded in %.3fms\n", cmd, (t1 - t0) / 1000.0);
    snprintf(cmd, sizeof(cmd), "build/dsp.%d.so", version - 1);
    unlink(cmd);
    return true;
}

int main(void) {
    printf("foobar - " __DATE__ " " __TIME__ "\n");
    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = 48000;
    cfg.capture.format = cfg.playback.format = ma_format_f32;
    cfg.capture.channels = cfg.playback.channels = 2;
    cfg.dataCallback = audio_cb;
    ma_device dev;
    if (ma_device_init(NULL, &cfg, &dev) != MA_SUCCESS) {
        fprintf(stderr, "ma_device_init failed\n");
        return 2;
    }
    if (ma_device_start(&dev) != MA_SUCCESS) {
        fprintf(stderr, "ma_device_start failed\n");
        ma_device_uninit(&dev);
        return 3;
    }
    time_t last = 0;
    for (;;) {
        usleep(20 * 1000);
        struct stat st;
        if (stat("dsp.c", &st) == 0 && st.st_mtime != last) {
            last = st.st_mtime;
            kick_compile();
        }
    }
}
