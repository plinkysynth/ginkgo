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

#define RVMASK 65535

static float k_reverb_fade = 240 / 256.f;                // 240 originally
static float k_reverb_shim = 10 / 256.f;                 // 80?
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

stereo limiter(float state[2], stereo inp) {
    const static float k_attack_ms = 0.5f;
    const static float k_decay_ms = 60.f;
    const static float k_attack = 1.f - expf(-1.f / (SAMPLE_RATE * k_attack_ms * 0.001f));
    const static float k_decay = 1.f - expf(-1.f / (SAMPLE_RATE * k_decay_ms * 0.001f));
    float peak = max(fabsf(inp.l), fabsf(inp.r));
    float &env_follow = state[0];
    float &hold_time = state[1];
    if (peak > env_follow) {
        hold_time = SAMPLE_RATE / 100; // 10ms hold time
        env_follow += (peak - env_follow) * k_attack;
    } else {
        if (hold_time > 0.f)
            hold_time--;
        else
            env_follow += (peak - env_follow) * k_decay;
    }
    float gain = 0.9f / (max(env_follow, 0.9f));
    // printf("gain: %f\n", lin2db(gain));
    return sclip(inp * gain);
}

stereo reverb_t::_run_internal(stereo input) {
    int i = reverb_pos;
    float outl = 0, outr = 0;
    float wob = aplfo(lforate1) * k_reverb_wob;
    float apwobpos = (wob + 1.f) * 64.f;
    wob = aplfo2(lforate2) * k_reverb_wob;
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

    acc += fb1;
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
        shimmerfade += dshimmerfade;

        if (shimmerfade >= SHIMMER_FADE_LEN) {
            shimmerfade -= SHIMMER_FADE_LEN;

            shimmerpos1 = shimmerpos2;
            shimmerpos2 = (rand() & 4095) + 8192;
            dshimmerfade = (rand() & 7) + 8; // somewhere between SHIMMER_FADE_LEN/2048 and SHIMMER_FADE_LEN/4096 ie 8 and 16
        }

        // L = shimmer from shimmerpos1, R = shimmer from shimmerpos2
        stereo shim1 = st(reverbbuf[(i + shimmerpos1) & RVMASK], reverbbuf[(i + shimmerpos2) & RVMASK]);
        stereo shim2 = st(reverbbuf[(i + shimmerpos1 + 1) & RVMASK], reverbbuf[(i + shimmerpos2 + 1) & RVMASK]);
        stereo shim = (shim1 + shim2);

        // Fixed point crossfade:
        float shimo = shim.l * ((SHIMMER_FADE_LEN - 1) - shimmerfade) + shim.r * shimmerfade;
        shimo *= 1.f / SHIMMER_FADE_LEN; // Divide by SHIMMER_FADE_LEN

        // Apply user-selected shimmer amount.
        // Tone down shimmer amount.
        shimo *= k_reverb_shim * 0.25f;

        acc += shimo;
        outl = shimo;
        outr = shimo;

        shimmerpos1--;
        shimmerpos2--;
    }

    const static float k_reverb_color = 0.95f;
    lpf += (((acc * k_reverb_fade)) - lpf) * k_reverb_color;
    dc += (lpf - dc) * 0.005f;
    acc = (lpf - dc);
    outl += acc;

    acc += reinject;
    AP_WOBBLE(908, delaywobpos);
    AP(2656);
    DELAY(3163);
    lpf2 += (((acc * k_reverb_fade)) - lpf2) * k_reverb_color;
    acc = (lpf2);

    outr += acc;

    reverb_pos = (reverb_pos - 1) & RVMASK;
    fb1 = (acc * k_reverb_fade);
    return st(outl, outr);
}

stereo delay_t::operator()(stereo inp, stereo time_qn, float feedback, float rotate_angle) {
    stereo ret;
    time_qn *= (SAMPLE_RATE * 60.f) / G->bpm;
    stereo readpos = mono2st(delay_pos) - time_qn;
    int il = int(floorf(readpos.l)) & (delaybuf_size - 1);
    int ir = int(floorf(readpos.r)) & (delaybuf_size - 1);
    ret.l = lerp(delaybuf[il].l, delaybuf[(il + 1) & (delaybuf_size - 1)].l, frac(readpos.l));
    ret.r = lerp(delaybuf[ir].r, delaybuf[(ir + 1) & (delaybuf_size - 1)].r, frac(readpos.r));
    ret = rotate(ret, cosf(rotate_angle), sinf(rotate_angle));
    delaybuf[delay_pos] = ssclip(inp + ret * feedback);
    delay_pos = (delay_pos + 1) & (delaybuf_size - 1);
    return ret;
}

stereo reverb_t::operator()(stereo inp) {
    float *state = filter_state;
    stereo *state2 = (stereo *)(state + 8);
    ////////////////////////// 4x DOWNSAMPLE
    inp = (stereo){
        svf_process_2pole(state, inp.l, 0.5f, SQRT2).lp,
        svf_process_2pole(state + 2, inp.r, 0.5f, SQRT2).lp,
    };
    int outslot = (G->sampleidx >> 2) & 3;
    if ((G->sampleidx & 3) == 0) {
        state2[outslot] = _run_internal(inp);
    }
    ////////////////////////// 4x CATMULL ROM UPSAMPLE
    int t0 = (outslot - 3) & 3, t1 = (outslot - 2) & 3, t2 = (outslot - 1) & 3, t3 = outslot;
    float f = ((G->sampleidx & 3) + 1) * 0.25f;
    float lout = catmull_rom(state2[t0].l, state2[t1].l, state2[t2].l, state2[t3].l, f);
    float rout = catmull_rom(state2[t0].r, state2[t1].r, state2[t2].r, state2[t3].r, f);
    return (stereo){lout, rout};
}

void *dsp_preamble(song_base_t *_G, stereo *audio, int reloaded, size_t state_size, void (*init_state)(void)) {
    if (!_G || _G->_size != state_size) {
        /* free(G); - safer to just let it leak :)  virtual memory ftw  */
        // printf("CLEARING STATE\n");
        song_base_t *oldg = _G;
        _G = (song_base_t *)calloc(1, state_size);
        if (oldg) {
            size_t sz = sizeof(song_base_t); // min(sizeof(song_base_t), min(state_size, oldg->_size));
            memcpy(_G, oldg, sz); // preserve the basic state...
        }
        // ...but update the size.
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
    pattern_t *bpm_pattern = get_pattern("/bpm");
    if (bpm_pattern && bpm_pattern->key) {
        hap_t haps[8];
        hap_span_t hs = bpm_pattern->make_haps({haps, haps + 8}, 8, -1.f, _G->t + _G->dt * 0.5f);
        if (hs.s < hs.e) {
            float newbpm = hs.s->get_param(P_NUMBER, _G->bpm);
            if (newbpm > 20.f && newbpm < 400.f && newbpm != _G->bpm) {
                printf("setting bpm to %f\n", newbpm);
                _G->bpm = newbpm;
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
    hap_time to = G->t + G->dt * 96.5f; // go epsilon (half a sample) over to ensure no cracks :)
    hap_t haps[8];
    hap_span_t hs = {};
    pattern_t *p = get_pattern(pattern_name);
    if (p && G->playing)
        hs = p->make_haps({haps, haps + 8}, 8, G->iTime, to + G->dt);
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

stereo voice_state_t::synth_sample(hap_t *h, bool keydown, float env1, float env2, float fold_actual,
                                   float dist_actual, float cutoff_actual, wave_t *w) {
    stereo au;
    bool at_end = false;
    if (w && w->sample_func) {
        au = (w->sample_func)(this, h, w, &at_end);
        phase += dphase * (double(w->sample_rate) / SAMPLE_RATE);
    } else {
        au = {};
    }
    in_use = EInUse::IN_USE;
    au *= env1;
    if (fold_actual > 0.f) {
        float foldgain = fold_actual * fold_actual * 100.f + 1.f;
        // float invfoldgain = 1.f / (fold_actual * 10.f + 1.f);
        au.l = fold(au.l * foldgain); // * invfoldgain;
        au.r = fold(au.r * foldgain); // * invfoldgain;
    }
    if (dist_actual > 0.f) {
        float mix = saturate(dist_actual * 10.f);
        float distgain = exp(saturate(dist_actual) * 16.f);
        au.l = lerp(au.l, sclip(au.l * distgain), mix);
        au.r = lerp(au.r, sclip(au.r * distgain), mix);
    }
    float resonance = h->get_param(P_RESONANCE, 0.f);
    au = filter.lpf(au, cutoff_actual, saturate(1.f - resonance));
    if (!isfinite(au.l) || !isfinite(au.r)) {
        au = {};
        filter = {};
    }
    float gain = h->get_param(P_GAIN, 0.75f);
    gain = gain * gain; // gain curve
    float panpos = h->get_param(P_PAN, 0.5f);
    au = pan(au, panpos * 2.f - 1.f);
    au *= gain;
    if (!keydown && (env1 < 0.01f || at_end)) {
        h->valid_params = 0;
        in_use = EInUse::UNUSED;
    }
    return au;
}

stereo synth_t::operator()(const char *pattern_name, float level, int max_voices) {
    if (max_voices > synth_t::max_voices)
        max_voices = synth_t::max_voices;
    if (max_voices < 1)
        return {};
    if (level < 0.f)
        level = 0.f;
    level = level * level;
    if ((G->sampleidx % 96) == 0) {
        memset(audio, 0, sizeof(audio));
        pattern_t *p = get_pattern(pattern_name);
        hap_time from = G->t;
        hap_time to = G->t + G->dt * 96.5f; // go epsilon (half a sample) over to ensure no cracks :)
        hap_t haps[8];
        hap_span_t hs = {};
        if (p && G->playing)
            hs = p->make_haps({haps, haps + 8}, 8, (level > 0.f) ? G->iTime : -1.f, to);
        pretty_print_haps(hs, from, to);
        for (hap_t *h = hs.s; h < hs.e; h++) {
            if (!(h->valid_params & ((1 << P_NOTE) | (1 << P_SOUND)) && h->t0 < to && h->t1 >= from))
                continue;
            int i = (int)h->get_param(P_STRING, -1);
            if (i<0 || i>=max_voices) {
                for (; i < max_voices; ++i) {
                    hap_t *existing_hap = &voices[i].h;
                    if (existing_hap->hapid == h->hapid && existing_hap->t0 == h->t0 && existing_hap->valid_params)
                        break;
                }
                if (i == max_voices) {
                    if (h->t0 >= to || h->t0 < from)
                        continue; // only trigger voices at the right time.
                    // ok its a new voice.
                    // find oldest one to steal
                    i = 0;
                    double oldest_t = voices[0].h.t0;
                    for (int j = 1; j < max_voices; ++j) {
                        if (voices[j].in_use == EInUse::UNUSED) {
                            i = j;
                            break;
                        }
                        if (voices[j].h.t0 < oldest_t) {
                            oldest_t = voices[j].h.t0;
                            i = j;
                        }
                    }
                    voices[i].h.hapid = 0;
                    voices[i].h.t0 = h->t0;
                    voices[i].h.t1 = h->t1;
                    voices[i].in_use = EInUse::ALLOCATED;
                    voices[i].vibphase = rnd01();
                    voices[i].tremphase = 0.f;
                }
            }
            assert(i < max_voices);
            hap_t *dst = &voices[i].h;
            if (dst->hapid != h->hapid) {
                // hapid has changed. often that's when the voice allocation code above ran,
                // but for a monophonic voice where 'string' was specified, it may just be the note changing.
                dst->hapid = h->hapid;
                dst->node = h->node;
                dst->valid_params = 0;
                dst->scale_bits = 0;
                dst->t1 = h->t1; // we ONLY update t1, not t0; that way a sustained monosynth note doesnt get a 'hole' in it
                // due to first_smpl below.
                // however we do set inuse to allocated, to force an envelope retrig.
                voices[i].in_use = EInUse::ALLOCATED;
            }
            merge_hap(dst, h);
        }
        for (int i = 0; i < max_voices; ++i) {
            voice_state_t *v = &voices[i];
            hap_t *h = &v->h;
            if (!h->valid_params)
                continue;
            static int sawidx = get_sound_index("saw");
            int sound = h->get_param(P_SOUND, sawidx);
            int first_smpl = (h->t0 - from) / G->dt;
            if (first_smpl >= 96) {
                // printf("FIRST SAMPLE OUT OF BOUNDS. SKIPPING %d\n", i);
                continue;
            }
            if (first_smpl < 0)
                first_smpl = 0;
            float note = h->get_param(P_NOTE, C3);
            float number = h->get_param(P_NUMBER, 0.f);
            wave_t *w = get_wave(get_sound_by_index(sound), number, &note);
            float vib = h->get_param(P_VIB, 0.f);
            float vibfreq = h->get_param(P_VIB_FREQ, 1.5f);
            note += sino(v->vibphase) * vib;
            v->vibphase = frac(v->vibphase + vibfreq * 96.f * 8.f * G->dt);
            float trem = h->get_param(P_TREM, 0.f) * 0.5f;
            float tremfreq = h->get_param(P_TREM_FREQ, 1.5f);
            float oldtrem = square(1.f-trem-trem*cosf(v->tremphase * TAU));
            v->tremphase = frac(v->tremphase + tremfreq * 96.f * 8.f * G->dt);
            float newtrem = square(1.f-trem-trem*cosf(v->tremphase * TAU));


            double dphase = exp2((note - C3) / 12.f); // midi2dphase(note);
            float key_track_gain = exp2((note - C3) / -36.f); // gentle falloff of high notes

            level *= key_track_gain;

            float newlevel = level * newtrem;
            level = level * oldtrem;
            float dlevel = (newlevel-level)/96.f;



            float sustain = square(h->get_param(P_SUS, 1.f));
            float attack_k = env_k(h->get_param(P_ATT, 0.f));
            float decay_k = env_k(h->get_param(P_DEC, 0.3f));
            float release = (h->get_param(P_REL, -1.f));
            float release_k;
            if (release >= 0.f) {
                release_k = env_k(release);
            } else {
                // no release specified...
                if (w && w->num_frames > 0 && h->get_param(P_LOOPS, 0.f) >= h->get_param(P_LOOPE, 0.f)) {
                    release_k = 0.f; // it's a one shot sample so let it play out. nb this is now a _k value, not a time.
                } else {
                    release_k = 0.005f; // it's a looping sample, so we must immediately stop when the key goes up.
                }
            }
            float sustain2 = square(h->get_param(P_SUS2, 1.f));
            float attack2_k = env_k(h->get_param(P_ATT2, 0.f));
            float decay2_k = env_k(h->get_param(P_DEC2, 0.3f));
            float release2_k = env_k(h->get_param(P_REL2, 1000.f));

            float gate = h->get_param(P_GATE, 0.75f);
            float cutoff = h->get_param(P_CUTOFF, 24000.f);
            float dist = h->get_param(P_DIST, 0.f);
            float fold_amount = h->get_param(P_FOLD, 0.f);
            float env2vcf = h->get_param(P_ENV2VCF, 1.f);
            float env2dist = h->get_param(P_ENV2DIST, 0.f);
            float env2fold = h->get_param(P_ENV2FOLD, 0.f);
            float k_glide = env_k(0.5f * h->get_param(P_GLIDE, 0.f));
            hap_time t = from + first_smpl * G->dt;
            if (!G->playing) {
                v->h.hapid = 0;
            }
            if (v->phase <=0.) v->phase=dphase;
            for (int smpl = first_smpl; smpl < 96; smpl++, t += G->dt) {
                ////////////// VOICE SAMPLE
                bool keydown = G->playing && t < h->t1 + G->dt * 0.5f;
                bool retrig = v->in_use < EInUse::IN_USE;
                float env1 = v->adsr1(keydown ? gate : 0.f, attack_k, decay_k, sustain, release_k, retrig);
                float env2 = v->adsr2(keydown ? gate : 0.f, attack2_k, decay2_k, sustain2, release2_k, retrig);
                float cutoff_actual = max(20.f, cutoff * (min(1.f, 1.f - env2vcf) + env2 * env2vcf));
                float fold_actual = fold_amount * (min(1.f, 1.f - env2fold) + env2 * env2fold);
                float dist_actual = dist * (min(1.f, 1.f - env2dist) + env2 * env2dist);
                v->dphase += (dphase - v->dphase) * k_glide;
                v->dphase = dphase;
                audio[smpl] += v->synth_sample(h, keydown, env1, env2, fold_actual, dist_actual, cutoff_actual, w) * level;
                level += dlevel;
                if (v->in_use != EInUse::IN_USE) {
                    printf("voice %d released - from %f to %f\n", i, from, to);
                    h->t0 = -1e10; // mark it as 'old'.
                    break;
                }
            } // voice sample loop
        } // voice loop
    } // 96 sample block loop
    return audio[G->sampleidx % 96];
}

stereo prepare_preview(void) {
    stereo preview = {};
    if (G->preview_wave_idx_plus_one) {
        wave_t *w = &G->waves[G->preview_wave_idx_plus_one - 1];
        if (w) {
            float pos = w->sample_rate * G->preview_wave_t;
            G->preview_wave_t += 1.f / SAMPLE_RATE;
            bool at_end = false;
            voice_state_t v = {.dphase = 1.f/SAMPLE_RATE, .phase = pos};
            hap_t h = { .valid_params = (1 << P_FROM) | (1 << P_TO), 
                .params[P_FROM] = G->preview_fromt,
                .params[P_TO] = G->preview_tot,
            };
            preview =
                sample_wave(&v, &h, w, &at_end) * (G->preview_wave_fade * 0.33f);
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

static float _silence[16] = {};

void register_osc(const char *name, sample_func_t *func) {
    wave_t w = {.key = name, .sample_func = func, .sample_rate = SAMPLE_RATE, .frames = _silence, .num_frames = 0};
    stbds_shputs(G->waves, w);
    int idx = stbds_shgeti(G->waves, w.key);
    Sound *osc = get_sound_init_only(name);
    stbds_arrsetlen(osc->wave_indices, 0);
    stbds_arrpush(osc->wave_indices, idx);
}
