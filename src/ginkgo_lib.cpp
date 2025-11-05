// this c file is linked into the dsp.c as well as the main program
// it's essentially 'precompiled stuff' :)
#define PFFFT_IMPLEMENTATION
#include "3rdparty/pffft.h"
#include "ginkgo.h"
#include "utils.h"
#include "wavfile.h"
#define STB_DS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"
#include "miniparse.h"
#include "multiband.h"

basic_state_t dummy_state;
basic_state_t *_BG = &dummy_state;

void init_basic_state(void) { _BG->bpm = 120.f; }

#define RVMASK 65535

static float k_reverb_fade = 220 / 256.f; // 240 originally
static float k_reverb_shim = 80 / 256.f;
static float k_reverb_wob = 0.3333f;                     // amount of wobble
static const float lforate1 = 1.f / 32777.f * 9.4f;      // * 2.f; // speed of wobble
static const float lforate2 = 1.3f / 32777.f * 3.15971f; // * 2.f; // speed of wobble

static inline float LINEARINTERPRV(const float *buf, int basei, float wobpos) { // read buf[basei-wobpos>>12] basically
    basei -= (int)wobpos;
    wobpos -= floorf(wobpos);
    float a0 = buf[basei & RVMASK];
    float a1 = buf[(basei - 1) & RVMASK];
    return ((a0 * (1.f - wobpos) + a1 * wobpos));
}

stereo plinky_reverb(stereo input) {
    reverb_state_t *R = &G->R;
    float *reverbbuf = R->reverbbuf;
    int i = R->reverb_pos;
    float outl = 0, outr = 0;
    float wob = update_lfo(R->aplfo, lforate1) * k_reverb_wob;
    float apwobpos = (wob + 1.f) * 64.f;
    wob = update_lfo(R->aplfo2, lforate2) * k_reverb_wob;
    float delaywobpos = (wob + 1.f) * 64.f;
#define RVDIV / 2
#define CHECKACC // assert(acc>=-32768 && acc<32767);
#define AP(len)                                                                                                                    \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        float d = reverbbuf[j];                                                                                                    \
        acc -= d * 0.5f;                                                                                                           \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = (acc * 0.5f) + d;                                                                                                    \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define AP_WOBBLE(len, wobpos)                                                                                                     \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        float d = LINEARINTERPRV(reverbbuf, j, wobpos);                                                                            \
        acc -= d * 0.5f;                                                                                                           \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = (acc * 0.5f) + d;                                                                                                    \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define DELAY(len)                                                                                                                 \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = reverbbuf[j];                                                                                                        \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }
#define DELAY_WOBBLE(len, wobpos)                                                                                                  \
    {                                                                                                                              \
        int j = (i + len RVDIV) & RVMASK;                                                                                          \
        reverbbuf[i] = (acc);                                                                                                      \
        acc = LINEARINTERPRV(reverbbuf, j, wobpos);                                                                                \
        i = j;                                                                                                                     \
        CHECKACC                                                                                                                   \
    }

    float acc = ((input.l));
    AP(142);
    AP(379);
    acc += (input.r);
    AP(107);
    //	float reinject2 = acc;
    AP(277);
    float reinject = acc;

    acc += R->fb1;
    AP_WOBBLE(672, apwobpos);
    AP(1800);
    DELAY(4453);

    if (1) {
        // shimmer - we can read from up to about 2000 samples ago

        // Brief shimmer walkthrough:
        // - We walk backwards through the reverb buffer with 2 indices: shimmerpos1 and shimmerpos2.
        //   - shimmerpos1 is the *previous* shimmer position.
        //   - shimmerpos2 is the *current* shimmer position.
        //   - Note that we add these to i (based on reverbpos), which is also walking backwards
        //     through the buffer.
        // - shimmerfade controls the crossfade between the shimmer from shimmerpos1 and shimmerpos2.
        //   - When shimmerfade == 0, shimmerpos1 (the old shimmer) is chosen.
        //   - When shimmerfade == SHIMMER_FADE_LEN - 1, shimmerpos2 (the new shimmer) is chosen.
        //   - For everything in-between, we linearly interpolate (crossfade).
        //   - When we hit the end of the fade, we reset shimmerpos2 to a random new position and set
        //     shimmerpos1 to the old shimmerpos2.
        // - dshimmerfade controls the speed at which we fade.

#define SHIMMER_FADE_LEN 32768
        R->shimmerfade += R->dshimmerfade;

        if (R->shimmerfade >= SHIMMER_FADE_LEN) {
            R->shimmerfade -= SHIMMER_FADE_LEN;

            R->shimmerpos1 = R->shimmerpos2;
            R->shimmerpos2 = (rand() & 4095) + 8192;
            R->dshimmerfade = (rand() & 7) + 8; // somewhere between SHIMMER_FADE_LEN/2048 and SHIMMER_FADE_LEN/4096 ie 8 and 16
        }

        // L = shimmer from shimmerpos1, R = shimmer from shimmerpos2
        stereo shim1 = st(reverbbuf[(i + R->shimmerpos1) & RVMASK], reverbbuf[(i + R->shimmerpos2) & RVMASK]);
        stereo shim2 = st(reverbbuf[(i + R->shimmerpos1 + 1) & RVMASK], reverbbuf[(i + R->shimmerpos2 + 1) & RVMASK]);
        stereo shim = (shim1 + shim2);

        // Fixed point crossfade:
        float shimo = shim.l * ((SHIMMER_FADE_LEN - 1) - R->shimmerfade) + shim.r * R->shimmerfade;
        shimo *= 1.f / SHIMMER_FADE_LEN; // Divide by SHIMMER_FADE_LEN

        // Apply user-selected shimmer amount.
        // Tone down shimmer amount.
        shimo *= k_reverb_shim * 0.25f;

        acc += shimo;
        outl = shimo;
        outr = shimo;

        R->shimmerpos1--;
        R->shimmerpos2--;
    }

    const static float k_reverb_color = 0.95f;
    R->lpf += (((acc * k_reverb_fade)) - R->lpf) * k_reverb_color;
    R->dc += (R->lpf - R->dc) * 0.005f;
    acc = (R->lpf - R->dc);
    outl += acc;

    acc += reinject;
    AP_WOBBLE(908, delaywobpos);
    AP(2656);
    DELAY(3163);
    R->lpf2 += (((acc * k_reverb_fade)) - R->lpf2) * k_reverb_color;
    acc = (R->lpf2);

    outr += acc;

    R->reverb_pos = (R->reverb_pos - 1) & RVMASK;
    R->fb1 = (acc * k_reverb_fade);
    return st(outl, outr);
}

stereo delay(stereo inp, stereo time_qn, float feedback, float rotate_angle) {
    delay_state_t *D = &G->D;
    stereo ret;
    time_qn *= (SAMPLE_RATE * 60.f) / G->bpm;
    stereo readpos = mono2st(D->delay_pos) - time_qn;
    int il = int(floorf(readpos.l)) & 65535;
    int ir = int(floorf(readpos.r)) & 65535;
    ret.l = lerp(D->delaybuf[il].l, D->delaybuf[(il + 1) & 65535].l, frac(readpos.l));
    ret.r = lerp(D->delaybuf[ir].r, D->delaybuf[(ir + 1) & 65535].r, frac(readpos.r));
    ret = rotate(ret, cosf(rotate_angle), sinf(rotate_angle));
    D->delaybuf[D->delay_pos] = ssclip(inp + ret * feedback);
    D->delay_pos = (D->delay_pos + 1) & 65535;
    return ret;
}

stereo reverb(stereo inp) {
    float *state = G->R.filter_state;
    stereo *state2 = (stereo *)(state + 8);
    ////////////////////////// 4x DOWNSAMPLE
    inp = (stereo){
        svf_process_2pole(state, inp.l, 0.5f, SQRT2).lp,
        svf_process_2pole(state + 2, inp.r, 0.5f, SQRT2).lp,
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        state2[outslot] = plinky_reverb(inp);
    }
    ////////////////////////// 4x CATMULL ROM UPSAMPLE
    int t0 = (outslot - 3) & 3, t1 = (outslot - 2) & 3, t2 = (outslot - 1) & 3, t3 = outslot;
    float f = ((G->sampleidx & 3) + 1) * 0.25f;
    float lout = catmull_rom(state2[t0].l, state2[t1].l, state2[t2].l, state2[t3].l, f);
    float rout = catmull_rom(state2[t0].r, state2[t1].r, state2[t2].r, state2[t3].r, f);
    return (stereo){lout, rout};
}

void *dsp_preamble(basic_state_t *_G, stereo *audio, int reloaded, size_t state_size, int version, void (*init_state)(void)) {
    if (!_G || _G->_ver != version || _G->_size != state_size) {
        /* free(G); - safer to just let it leak :)  virtual memory ftw  */
        // printf("CLEARING STATE\n");
        basic_state_t *oldg = _G;
        _G = (basic_state_t *)calloc(1, state_size);
        if (oldg)
            memcpy(_G, oldg, sizeof(basic_state_t)); // preserve the basic state...
        // ...but update the version number and size.
        _G->_ver = version;
        _G->_size = state_size;
    }
    _G->reloaded = reloaded;
    G = _G; // set global variable for user code. nb the dll and the main program have their own copies of G. dsp() ends up setting
            // the dll copy from the main process copy.
    if (reloaded) {
        init_basic_state();
        (*init_state)();
    }
    // update bpm
    if (_G->patterns_map) {
        pattern_t *bpm_pattern = stbds_shgetp(_G->patterns_map, "/bpm");
        if (bpm_pattern && bpm_pattern->key) {
            hap_t haps[8];
            hap_span_t hs = bpm_pattern->make_haps({haps, haps + 8}, 8, -1.f, _G->t, _G->t + hap_eps);
            if (hs.s < hs.e) {
                float newbpm = hs.s->get_param(P_NUMBER, _G->bpm);
                if (newbpm > 20.f && newbpm < 400.f && newbpm != _G->bpm) {
                    printf("setting bpm to %f\n", newbpm);
                    _G->bpm = newbpm;
                }
            }
        }
    }
    return _G;
}

wave_t *request_wave_load(wave_t *wave) {
    if (!G || !wave)
        return NULL;
    if (wave->frames)
        return wave;
    if (wave->download_in_progress)
        return NULL;
    spin_lock(&G->load_request_cs);
    wave_request_t key = {wave};
    stbds_hmputs(G->load_requests, key);
    spin_unlock(&G->load_request_cs);
    return NULL;
}

// conv ir block sizes are 256, 512, 1024, 2048...
#define CONV_IR_NUM_BLOCK_SIZES 5
#define CONV_IR_MIN_BLOCK_SIZE 256 // fft sizes are double the block size (zero pad), and complex (*2 again)
#define CONV_IR_MAX_BLOCK_SIZE (CONV_IR_MIN_BLOCK_SIZE << (CONV_IR_NUM_BLOCK_SIZES - 1))

inline stereo complexmul(stereo a, stereo b, float scale) {
    return st((a.l * b.l - a.r * b.r) * scale, (a.l * b.r + a.r * b.l) * scale);
}

// building blocks for conv reverb
void test_conv_reverb(void) {
    PFFFT_Setup *fft_setup[CONV_IR_NUM_BLOCK_SIZES] = {};
    static float *fft_work = 0;
    if (!fft_setup[0]) {
        fft_work = (float *)malloc(CONV_IR_MAX_BLOCK_SIZE * 4 * sizeof(float));
        for (int i = 0; i < CONV_IR_NUM_BLOCK_SIZES; i++) {
            fft_setup[i] = pffft_new_setup(CONV_IR_MIN_BLOCK_SIZE << (i + 1), PFFFT_COMPLEX);
        }
    }
    stereo *testblock = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    float e = 1.f;
    for (int i = 0; i < CONV_IR_MAX_BLOCK_SIZE; i++) {
        testblock[i] = st(sinf(i * i * 0.0001f) * e, rndn() * e * 0.25f);
        e *= 0.999f;
    }

    stereo *irblock = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    irblock[1000].l = 1.f;

    stereo *irblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    stereo *testblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    stereo *convblock_fft = (stereo *)calloc(CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2 * 2);
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)irblock, (float *)irblock_fft, fft_work, PFFFT_FORWARD);
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)testblock, (float *)testblock_fft, fft_work, PFFFT_FORWARD);
    pffft_zconvolve_accumulate(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)irblock_fft, (float *)testblock_fft,
                               (float *)convblock_fft, 1.f / (CONV_IR_MAX_BLOCK_SIZE * 2.f));
    pffft_transform(fft_setup[CONV_IR_NUM_BLOCK_SIZES - 1], (float *)convblock_fft, (float *)convblock_fft, fft_work,
                    PFFFT_BACKWARD);

    FILE *f = fopen("testblock.wav", "wb");
    write_wav_header(f, CONV_IR_MAX_BLOCK_SIZE * 2, 48000, 2);
    fwrite(testblock, 2 * CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2, f);
    fclose(f);
    f = fopen("testblock2.wav", "wb");
    write_wav_header(f, CONV_IR_MAX_BLOCK_SIZE * 2, 48000, 2);
    fwrite(convblock_fft, 2 * CONV_IR_MAX_BLOCK_SIZE, sizeof(float) * 2, f);
    fclose(f);
}

hap_t *pat2hap(const char *pattern_name, hap_t *cache) {
    if ((G->sampleidx % 96) != 0)
        return cache;
    hap_time from = G->t;
    hap_time to = G->t + G->dt * 96.f;
    hap_t haps[8];
    hap_span_t hs = {};
    pattern_t *p = stbds_shgetp_null(G->patterns_map, pattern_name);
    if (p && G->playing)
        hs = p->make_haps({haps, haps + 8}, 8, G->iTime, from, to);
    // highest note priority
    float highest_note = -1;
    float highest_number = -1;
    int highest_index = -1;
    for (int i = 0; i < hs.e - hs.s; ++i) {
        hap_t *h = &haps[i];
        if (h->valid_params & ((1 << P_NOTE) | (1 << P_SOUND)) && h->t0 < to && h->t1 >= from) {
            float note = h->get_param(P_NOTE, 0.f);
            float number = h->get_param(P_NUMBER, 0.f);
            if (note >= highest_note || (note == highest_note && number > highest_number)) {
                highest_note = note;
                highest_number = number;
                highest_index = i;
            }
        }
    }
    if (highest_index != -1) {
        cache->hapid = haps[highest_index].hapid;
        merge_hap(cache, &haps[highest_index]);
    } else {
        cache->hapid = 0;
    }
    return cache;
}

stereo monosynth(const char *pattern_name, float glide, monosynth_state_t *state) {
    return {};  // TODO - pull out the innards of synth, and make it work for a mono synth with glide.
}


stereo synth(synth_state_t *synth, const char *pattern_name, float level, int max_voices) {
    if (max_voices > synth_state_t::max_voices)
        max_voices = synth_state_t::max_voices;
    if (max_voices < 1)
        return {};
    if (level < 0.f) level = 0.f;
    level = level*level;
    if ((G->sampleidx % 96) == 0) {
        memset(synth->audio, 0, sizeof(synth->audio));
        pattern_t *p = stbds_shgetp_null(G->patterns_map, pattern_name);
        hap_time from = G->t;
        hap_time to = G->t + G->dt * 96.f;
        hap_t haps[8];
        hap_span_t hs = {};
        if (p && G->playing)
            hs = p->make_haps({haps, haps + 8}, 8, (level>0.f) ? G->iTime : -1.f, from, to);
        pretty_print_haps(hs, from, to);
        uint32_t retrig = 0;
        for (hap_t *h = hs.s; h < hs.e; h++) {
            if (h->valid_params & ((1 << P_NOTE) | (1 << P_SOUND)) && h->t0 < to && h->t1 >= from) {
                int i = 0;
                for (; i < max_voices; ++i)
                    if (synth->v[i].h.hapid == h->hapid)
                        break;
                if (i == max_voices) {
                    if (h->t0 >= to + 0.001f || h->t0 < from - 0.001f)
                        continue; // only trigger voices at the right time.
                    // ok its a new voice.
                    // find oldest one to steal
                    i = 0;
                    double oldest_t = synth->v[0].h.t0;
                    for (int j = 1; j < max_voices; ++j)
                        if (synth->v[j].h.t0 < oldest_t || synth->v[j].h.valid_params == 0) {
                            oldest_t = synth->v[j].h.t0;
                            i = j;
                        }
                    // i=0; // MONOPHONIC DEBUG
                    hap_t *dst = &synth->v[i].h;
                    dst->hapid = h->hapid;
                    dst->node = h->node;
                    dst->valid_params = 0;
                    dst->scale_bits = 0;
                    dst->t0 = h->t0;
                    dst->t1 = h->t1;
                    synth->v[i].vibphase = rnd01();
                    retrig |= (1 << i);
                    printf("alloc voice %d - new = %d - len %f oldest %f\n", i, (int)h->get_param(P_NOTE, C3), h->t1 - h->t0,
                           oldest_t);
                }
                hap_t *dst = &synth->v[i].h;
                merge_hap(dst, h);
            }
        }
        for (int i = 0; i < max_voices; ++i) {
            voice_state_t *v = &synth->v[i];
            hap_t *h = &v->h;
            if (!h->valid_params)
                continue;
            int sound = h->get_param(P_SOUND, 0);
            int first_smpl = (h->t0 - from) / G->dt;
            if (first_smpl >= 96)
                continue;
            if (first_smpl < 0)
                first_smpl = 0;
            float note = h->get_param(P_NOTE, C3);
            float number = h->get_param(P_NUMBER, 0.f);
            wave_t *w = sound ? get_wave(get_sound_by_index(sound), number, &note) : NULL;
            float vib = h->get_param(P_VIB, 0.f);
            float vibfreq = h->get_param(P_VIB_FREQ, 0.5f);
            note += sino(v->vibphase) * vib * vib;
            v->vibphase = frac(v->vibphase + vibfreq * vibfreq * 3000.f * G->dt);

            double dphase = exp2((note - C3) / 12.f); // midi2dphase(note);
            float sustain = square(h->get_param(P_S, 1.f));
            float attack = env_k(h->get_param(P_A, 0.f));
            float decay = env_k(h->get_param(P_D, 0.f));
            float release = env_k(h->get_param(P_R, 0.f));
            float gate = h->get_param(P_GATE, 0.75f);
            float gain = h->get_param(P_GAIN, 0.75f);
            float loops = h->get_param(P_LOOPS, 0.f);
            float loope = h->get_param(P_LOOPE, 0.f);
            float panpos = h->get_param(P_PAN, 0.5f);

            float fromt = h->get_param(P_FROM, 0.f);
            float tot = h->get_param(P_TO, 1.f);
            // float cutoff = h->get_param(P_CUTOFF);
            // float resonance = h->get_param(P_RESONANCE);
            gain = gain * gain; // gain curve

            float duty = number * 0.125f + 0.0625f;
            hap_time t = from + first_smpl * G->dt;
            bool is_retrig = (retrig & (1 << i)) != 0;
            if (!G->playing) {
                v->h.hapid = 0;
            }
            for (int smpl = first_smpl; smpl < 96; smpl++, t += G->dt) {
                ////////////// VOICE SAMPLE
                bool keydown = G->playing && t < h->t1;
                float env = adsr(&v->env, keydown, attack, decay, sustain, release, is_retrig);
                if (is_retrig) {
                    v->phase = 0.f; /*printf("retrig %s\n", w->key);*/
                }
                bool at_end = false;
                if (!w) {
                    int i = 1;
                }
                stereo au;
                if (w) {
                    au = sample_wave(w, v->phase, loops, loope, fromt, tot, &at_end);
                    v->phase += dphase * (double(w->sample_rate) / SAMPLE_RATE);
                } else {
                    float osc_dphase = dphase * P_C3;
                    float phase = v->phase = frac(v->phase + osc_dphase);
                    float s;
                    if (number <= 0.f) {
                        s = sawo(phase, osc_dphase);
                        s = lerp(s, sino(phase + 0.25f), min(1.f, -number));
                    } else {
                        float duty = min(1.5f - number, 0.5f);
                        s = pwmo(phase, osc_dphase, duty, min(1.f, number));
                    }
                    au = mono2st(s);
                }
                au = pan(au, panpos * 2.f - 1.f);
                au *= env * gain * level; // / (v->dphase * 1000.f);
                synth->audio[smpl] += au;
                is_retrig = false;
                ////////////////// VOICE SAMPLE END
                // voice_state_buffers[i] = t_state_buffer;
                if (!keydown && (env < 0.01f || at_end)) {
                    printf("voice %d released\n", h->hapid);
                    h->valid_params = 0;
                    h->t0 = -1e10; // mark it as 'old'.
                    break;
                }
            } // voice sample loop
        } // voice loop
    } // 96 sample block loop
    return synth->audio[G->sampleidx % 96];
}

stereo prepare_preview(void) {
    stereo preview = {};
    if (G->preview_wave_idx_plus_one) {
        wave_t *w = &G->waves[G->preview_wave_idx_plus_one - 1];
        if (w) {
            float pos = w->sample_rate * G->preview_wave_t;
            G->preview_wave_t += 1.f / SAMPLE_RATE;
            preview = sample_wave(w, pos, 0.f, 0.f, G->preview_fromt, G->preview_tot) * (G->preview_wave_fade * 0.33f);
            if (pos >= w->num_frames) {
                G->preview_wave_fade = 0.f;
            }
        }
    }
    if (G->preview_wave_fade < 1.f) {
        G->preview_wave_fade *= 0.999f;
        if (G->preview_wave_fade < 0.0001f) {
            G->preview_wave_fade = 0.f;
        }
    }
    G->preview = preview;
    return preview;
}
