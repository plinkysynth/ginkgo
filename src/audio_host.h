#include "wavfile.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// hot reload and scope

#define SCOPE_SIZE 65536
#define FFT_SIZE 8192
#define SCOPE_MASK (SCOPE_SIZE - 1)
stereo scope[SCOPE_SIZE];
int xyscope[128][128];
uint32_t scope_pos = 0;


static _Atomic(dsp_fn_t) g_dsp_req = NULL;  // current callback
static _Atomic(dsp_fn_t) g_dsp_used = NULL; // what version the main thread compiled
static void *g_handle = NULL;
static int g_version = 0;
static FILE *wav_recording = NULL;
static void audio_cb(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    stereo *o = (stereo *)out;
    const stereo *i = (const stereo *)in;
    dsp_fn_t dsp = atomic_load_explicit(&g_dsp_req, memory_order_acquire);
    if (!dsp)
        return;
    dsp_fn_t old_dsp = atomic_load_explicit(&g_dsp_used, memory_order_acquire);
    stereo audio[OVERSAMPLE*frames];
    static stereo prev_input;
    static_assert(OVERSAMPLE == 2 || OVERSAMPLE == 1, "OVERSAMPLE must be 2 or 1");
    if (OVERSAMPLE == 2) {
        // upsample the input audio...
        for (int k = 0; k < frames; k++) {
            stereo new_input = i[k];
            audio[k * 2 + 0].l =(prev_input.l + new_input.l)* 0.5f;
            audio[k * 2 + 0].r =(prev_input.r + new_input.r)* 0.5f;
            audio[k * 2 + 1] = new_input;
            prev_input = new_input;
        }
    } else {
        memcpy(audio, i, frames * sizeof(stereo));
    }
    if (dsp)
        G = dsp(G, audio, (int)frames * OVERSAMPLE, dsp != old_dsp);
    // if (!wav_recording) {
    //     wav_recording = fopen("recording.wav", "wb");
    //     write_wav_header(wav_recording, 0, SAMPLE_RATE, 2);
    // }
    if (wav_recording) {
        fwrite(audio, sizeof(stereo), frames * OVERSAMPLE, wav_recording);
        int pos = ftell(wav_recording);
        fseek(wav_recording, 0, SEEK_SET);
        write_wav_header(wav_recording, G->sampleidx, SAMPLE_RATE, 2);
        fseek(wav_recording, 0, SEEK_END);
    }
    
    
    // downsample the output audio and update the scopes...
#define K 16 // the kernel has this many non-center non-zero taps.
    // example with K=3:
    // x . x . x 0.5 x . x . x <- x = non zero taps
    //                         ^- history_pos
    const float fir_center_tap = 0.5000461f;
    const static float fir_kernel[K] = {
        0.3171385f, -0.1026337f, 0.0580279f, -0.0378839f, 0.0260971f, -0.0182989f, 0.0128162f, -0.0088559f,
        0.0059815f, -0.0039141f, 0.0024594f, -0.0014665f, 0.0008178f, -0.0004156f, 0.0001849f, -0.0000690f,
    };
    static stereo history[64];
    static_assert(countof(history) >= (K * 4 - 1), "history too small");
    static uint32_t history_pos = 0;
    for (ma_uint32 k = 0; k < frames; ++k) {
        // saturation on output...
        stereo acc;
        if (OVERSAMPLE == 2) {
            history[history_pos & 63] = stereo_hard(ensure_finite_stereo(audio[k*2+0]));
            history[(history_pos + 1) & 63] = stereo_hard(ensure_finite_stereo(audio[k*2+1]));
            history_pos += 2;
            // 2x downsample FIR
            int center_idx = history_pos - K * 2;
            acc = history[center_idx & 63]; // center tap
            acc.l *= fir_center_tap;
            acc.r *= fir_center_tap;
            for (int tap = 0; tap < K; ++tap) {
                stereo t0 = history[(center_idx + tap * 2 + 1) & 63];
                stereo t1 = history[(center_idx - tap * 2 - 1) & 63];
                acc.l += fir_kernel[tap] * (t0.l + t1.l);
                acc.r += fir_kernel[tap] * (t0.r + t1.r);
            }
        } else {
            acc = stereo_hard(audio[k]);
        }
        o[k] = acc;
        scope[scope_pos & SCOPE_MASK] = acc;
        // vector scope
        static uint32_t wipe = 0;
        int *sc = &xyscope[(wipe >> 7) & 127][wipe & 127];
        sc[0] = (sc[0]) >> 2;
        sc[1] = (sc[1]) >> 2;
        sc[2] = (sc[2]) >> 2;
        sc[3] = (sc[3]) >> 2;
        wipe += 4;
        uint32_t x = (uint32_t)((acc.l) * 63.f + 64.f);
        uint32_t y = (uint32_t)((acc.r) * 63.f + 64.f);
        if (x < 128 && y < 128 && xyscope[y][x] < (1 << 23))
            xyscope[y][x] += 1024;
        scope_pos++;
    }
    atomic_store_explicit(&g_dsp_used, dsp, memory_order_release);
}

int64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

static bool try_to_compile_audio(const char *fname, char **errorlog) {
    if (errorlog) {
        stbds_arrfree(*errorlog);
        *errorlog = NULL;
    }
    char cmd[1024];
    int version = g_version + 1;
    mkdir("build", 0755);
    #define CLANG_OPTIONS "-x c -g -std=c11 -O2 -fPIC -dynamiclib -fno-caret-diagnostics -fno-color-diagnostics -Wno-comment -D LIVECODE -I. -Isrc/"
    snprintf(cmd, sizeof(cmd), "echo \"#include \\\"ginkgo.h\\\"\n#include \\\"%s\\\"\" |clang " CLANG_OPTIONS "  -o build/dsp.%d.so - 2>&1", fname, version);
    int64_t t0 = get_time_us();
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "popen %s failed: %s\n", cmd, strerror(errno));
        return false;
    }
    char buf[1024];
    int n = strlen(fname);
    fprintf(stderr, "compile %s\n", fname);
    
    while (fgets(buf, sizeof(buf), fp)) {
        fprintf(stderr, "%s", buf);
        int n = strlen(buf);
        char* errbuf = stbds_arraddnptr(*errorlog, n);
        memcpy(errbuf, buf, n);
        // char *errmsg = buf;
        // if (errmsg[0] == '.' && errmsg[1] == '/') errmsg+=2;
        // if (strncmp(errmsg, fname, n) == 0 && errmsg[n] == ':') {
        //     char *colend = errmsg+n+1;
        //     int col = strtol(colend, &colend, 10);
        //     int line = 0;
        //     if (*colend != ':') {
        //         line = col;
        //     } else line = strtol(colend+1, &colend, 10);
        //     if (line) {
        //         if (*colend==':') ++colend;
        //         while (isspace(*colend)) ++colend;
        //         //printf("compile error on line %d, column %d, [%s]\n", line, col, colend);
        //         hmput(*error_msgs, line - 1, colend);
        //     }
        // }
    }
    stbds_arrput(*errorlog, 0);
    int rc = pclose(fp);
    int64_t t1 = get_time_us();
    if (rc!=0)
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
