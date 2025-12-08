#define SJ_IMPL
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <ctype.h>
#include <string.h>
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "ginkgo.h"
#include "utils.h"
#include "miniparse.h"
#include "sampler.h"
#include "ansicols.h"
#include "http_fetch.h"
#include "json.h"
#include <assert.h>
#include "3rdparty/pffft.h"
const char *trimurl(const char *url) {
    // shorten url for printing
    if (strncmp(url, "https://", 8) == 0)
        url += 8;
    if (strncmp(url, "http://", 7) == 0)
        url += 7;
    if (strncmp(url, "raw.githubusercontent.com/", 26) == 0)
        url += 26;
    return url;
}

#ifdef FLUX
void compute_flux(wave_t *out) {
    #define FLUX_FFT_SIZE 512
    static PFFFT_Setup *fft_setup = NULL;
    static float flux_window[FLUX_FFT_SIZE];
    if (!fft_setup) {
        fft_setup = pffft_new_setup(FLUX_FFT_SIZE, PFFFT_REAL);
        for (int i = 0; i < FLUX_FFT_SIZE; i++) 
            flux_window[i] = 0.5f - 0.5f * cosf(TAU * (i+0.5f) / FLUX_FFT_SIZE);
    }
    float mags[2][FLUX_FFT_SIZE] = {0.f};
    float emaflux = 0.f;
    float peakflux = 1e-9f;
    float cumflux = 1e-9f;
    float *fluxes = NULL;
    int numflux = (out->num_frames+FLUX_HOP_LENGTH-1)/FLUX_HOP_LENGTH;
    stbds_arrsetlen(fluxes, numflux);
    for (int i = 0; i < out->num_frames; i += FLUX_HOP_LENGTH) {
        float inp[FLUX_FFT_SIZE*2] __attribute__((aligned(16)));
        for (int j = 0; j < FLUX_FFT_SIZE; j++) {
            if (i+j>=0 && i+j<out->num_frames) {
                float mono = 0.f;
                int idx = (i + j) * out->channels;

                for (int k=0; k<out->channels; k++) {
                    mono += out->frames[idx + k];
                }
                inp[j] = mono * flux_window[j];
            } else inp[j] = 0.f;
        }
        pffft_transform(fft_setup, inp, inp, NULL, PFFFT_FORWARD);
        int magidx = i&1;
        float flux = 0.f;
        for (int j=0; j<FLUX_FFT_SIZE/2; j++) {
            mags[magidx][j] = logf((0.0001f + square(inp[j*2]) + square(inp[j*2+1])));/// * j; // scale by j for pink noise weighting
            flux += max(0.f,mags[magidx][j] - mags[magidx^1][j]);
        }
        emaflux += (flux - emaflux) * (flux > emaflux ? 0.5f : 0.1f);
        flux = max(0.f, flux - emaflux);
        if (flux > peakflux) peakflux = flux;
        assert(i/FLUX_HOP_LENGTH < numflux);
        fluxes[i/FLUX_HOP_LENGTH] = flux;
        cumflux += flux;
    }
    out->flux = fluxes;
    // build an inverse cdf of the flux
    float fluxsofar = 0.f;
    int cdfidx = 0;
    stbds_arrsetlen(out->invflux, 256);
    for (int i =0 ; i<numflux; i++) {
        fluxsofar += (fluxes[i] / cumflux) * 256.f;
        while (cdfidx < 256 && fluxsofar >= cdfidx) 
            out->invflux[cdfidx++] = (float)i / float(numflux);
        fluxes[i] /= peakflux;
    }
    while (cdfidx < 256)
        out->invflux[cdfidx++] = 1.f;
}
#endif

bool decode_file_to_f32(const char *path, wave_t *out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // keep source ch/sr
    float *pcm = NULL;
    ma_uint64 num_frames = 0;
    ma_result rc = ma_decode_file(path, &cfg, &num_frames, (void **)&pcm);
    if (rc != MA_SUCCESS || num_frames == 0) {
        free(pcm);
        return false;
    }
    out->num_frames = num_frames;
    out->sample_rate = cfg.sampleRate;
    out->channels = cfg.channels;
    out->frames = pcm;
    #ifdef FLUX
    compute_flux(out);
    #endif
    //const char *trimmed_path = strrchr(path, '/');
    // printf("read %lld samples @ %dkhz %dchannels from " COLOR_YELLOW "%s" COLOR_RESET "\n", out->num_frames,
    //        (int)out->sample_rate / 1000, out->channels, trimmed_path ? trimmed_path + 1 : path);
    
    
    return true;
}

static float _silence[16] = {};

static void wave_loaded(const char *url, const char *fname, void *userdata) {
    wave_t *wave = (wave_t *)userdata;
    if (fname) {
        decode_file_to_f32(fname, wave);
    }
    if (!wave->frames) {
        fprintf(stderr, "warning: wave %s failed to load. replacing with silence\n", wave->key);
        wave->num_frames = 1;
        wave->sample_rate = 48000;
        wave->channels = 1;
        wave->frames = _silence;
    }
    wave->download_in_progress = 0;
    spin_lock(&G->load_request_cs);
    // printf("num load requests: %d\n", (int)stbds_hmlen(G->load_requests));
    stbds_hmdel(G->load_requests, wave);
    // printf("num load requests: %d\n", (int)stbds_hmlen(G->load_requests));
    spin_unlock(&G->load_request_cs);

}

void load_wave_now(wave_t *wave) {
    if (wave->frames || wave->download_in_progress>=2)
        return; // already loaded
    wave->download_in_progress = 2;
    #define ASYNC_WAVE_LOADING 
    #ifdef ASYNC_WAVE_LOADING
    const char *fname = fetch_to_cache(wave->key, 1 , wave_loaded, wave);
    #else
    const char *fname = fetch_to_cache(wave->key, 1);
    wave_loaded(wave->key, fname, wave);
    #endif
}

int parse_strudel_alias_json(const char *json_url, bool prefer_offline) {
    const char *json_fname = fetch_to_cache(json_url, prefer_offline);
    sj_Reader r = read_json_file(json_fname);
    int old_num_sounds = num_sounds();
    for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
        if (outer.val.type == SJ_STRING) {
            char *long_name = stbstring_from_span(outer.val.start, outer.val.end, 0);
            char *short_name = stbstring_from_span(outer.key.start, outer.key.end, 0);
            // printf("alias %s -> %s\n", long_name, short_name);
            add_alias_init_only(short_name, long_name);
        }
    }
    printf(COLOR_YELLOW "%s" COLOR_RESET " added " COLOR_GREEN "%d" COLOR_RESET " new aliases\n", trimurl(json_url),
           num_sounds() - old_num_sounds);
    free_json(&r);
    return 1;
}

static inline bool char_needs_escaping(const char *s, const char *e) {
    if (*s == '%' && (s + 1 < e) && s[1] == '_')
        return true;
    return *s == ' ' || *s == '#';
    // return !(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c=='/');
}

static inline int get_url_length_after_escaping(const char *s, const char *e) {
    int len = e - s;
    for (; s < e; s++)
        if (char_needs_escaping(s, e))
            len += 2;
    return len;
}

static inline const char *escape_url(char *dstbuf, const char *s, const char *e) {
    const static char hex[17] = "0123456789abcdef";
    char *d = dstbuf;
    for (; s < e; s++) {
        if (char_needs_escaping(s, e)) {
            *d++ = '%';
            *d++ = hex[*s >> 4];
            *d++ = hex[*s & 0xf];
        } else {
            *d++ = *s;
        }
    }
    *d = 0; // null terminator
    return dstbuf;
}

int parse_strudel_json(const char *json_url, bool prefer_offline, const char *sound_prefix = NULL) {

    char pathbuf[512];
    if (strncmp(json_url, "github:", 7) == 0) {
        // example raw url: https://raw.githubusercontent.com/sonidosingapura/blu-mar-ten/main/Breaks/strudel.json
        int slash_count = 0;
        for (const char *p = json_url+7; *p; p++) {
            if (*p == '/')
                ++slash_count;
        }
        if (slash_count == 1) {
            snprintf(pathbuf, sizeof(pathbuf), "https://raw.githubusercontent.com/%s/main/strudel.json", json_url+7);
        } else {
            snprintf(pathbuf, sizeof(pathbuf), "https://raw.githubusercontent.com/%s/strudel.json", json_url+7);
        }
        json_url = pathbuf;
    }

    int prefix_len = sound_prefix ? strlen(sound_prefix) : 0;
    const char *json_fname = fetch_to_cache(json_url, prefer_offline);
    sj_Reader r = read_json_file(json_fname);
    int old_num_sounds = num_sounds();
    int wav_count = 0, skip_count = 0;
    sj_Value base = {};
    for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
        if (iter_key_is(&outer, "_base")) {
            // printf("base is %.*s\n", (int)(soundval.end-soundval.start), soundval.start);
            base = outer.val;
        } else {
            int sound_name_len = (outer.key.end - outer.key.start) + prefix_len;
            char sound_name[sound_name_len + 1];
            memcpy(sound_name, sound_prefix, prefix_len);
            memcpy(sound_name + prefix_len, outer.key.start, outer.key.end - outer.key.start);
            sound_name[sound_name_len] = 0;
            Sound *sound = get_sound_init_only(sound_name);
            int old_wave_count = stbds_arrlen(sound->wave_indices);
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                char *url = NULL;
                int numbase = base.end - base.start;
                int numurl = get_url_length_after_escaping(inner.val.start, inner.val.end);
                stbds_arrsetlen(url, numbase + numurl + 1);
                memcpy(url, base.start, numbase);
                url[numbase] = 0;
                if (strncmp(url, "file://", 7) == 0 || strstr(url, "://") == 0) {
                    memcpy(url + numbase, inner.val.start, inner.val.end - inner.val.start);
                    url[numbase + (inner.val.end - inner.val.start)] = 0;
                } else {
                    escape_url(url + numbase, inner.val.start, inner.val.end);
                }
                int midinote = parse_midinote(inner.key.start, inner.key.end, NULL, false);
                // printf("%.*s (%d) -> %s\n", (int)(samplekey.end-samplekey.start), samplekey.start, midinote, url);
                int j;
                for (j = 0; j < stbds_arrlen(sound->wave_indices); ++j)
                    if (strcmp(G->waves[sound->wave_indices[j]].key, url) == 0)
                        break;
                if (j == stbds_arrlen(sound->wave_indices)) {
                    wav_count++;
                    int waveidx = stbds_shgeti(G->waves, url);
                    if (waveidx == -1) {
                        wave_t nw = {.key = url, .midi_note = midinote};
                        stbds_shputs(G->waves, nw);
                        waveidx = stbds_shgeti(G->waves, url);
                        assert(waveidx != -1);
                    }
                    stbds_arrput(
                        sound->wave_indices,
                        waveidx); // TODO; if this causes sound->waves to grow, we may get an exception on the other thread...
                    if (midinote >= 0) {
                        int_pair_t pair = {.k = midinote, .v = j};
                        int insert_at = lower_bound_int_pair(sound->midi_notes, stbds_arrlen(sound->midi_notes), pair);
                        stbds_arrins(sound->midi_notes, insert_at, pair);
                    }
                } else
                    skip_count++;
            }
            // log the number of waves in this sound
            // printf("%s -> %d waves\n", sound_name, (int)stbds_arrlen(sound->waves));
            if (old_wave_count && old_wave_count != stbds_arrlen(sound->wave_indices)) {
                printf("\tsound " COLOR_CYAN "%s" COLOR_RESET " already had " COLOR_GREEN "%d" COLOR_RESET " waves; now it has " COLOR_GREEN "%d" COLOR_RESET "\n", sound_name, old_wave_count, (int)stbds_arrlen(sound->wave_indices));
            }

        }
    }
    printf(COLOR_YELLOW "%s" COLOR_RESET ", %d new sounds, added " COLOR_GREEN "%d" COLOR_RESET " waves\n", trimurl(json_url),
           num_sounds() - old_num_sounds, wav_count);
    free_json(&r);
    return 1;
}

int num_waves(void) {
    int n = num_sounds();
    int numwaves = 0;
    for (int i = 0; i < n; ++i)
        numwaves += stbds_arrlen(G->sounds[i].value->wave_indices);
    return numwaves;
}
void dump_all_sounds(const char *fname) {
    FILE *f = fopen(fname, "w");
    fprintf(f, "{\n");
    for (int i = 0; i < num_sounds(); i++) {
        Sound *s = G->sounds[i].value;
        bool has_notes = stbds_arrlen(s->midi_notes) > 0;
        if (i)
            fprintf(f, ",\n");
        if (has_notes) {
            fprintf(f, "  \"%s\": {\n", s->name);
            for (int j = 0; j < stbds_arrlen(s->midi_notes); j++) {
                wave_t *w = &G->waves[s->wave_indices[s->midi_notes[j].v]];
                char comma = j == stbds_arrlen(s->midi_notes) - 1 ? ' ' : ',';
                // printf("midi note %s -> %s\n", print_midinote(w->midi_note), w->url);
                fprintf(f, "    \"%s\": \"%s\"%c // %d %dhz %dch\n", print_midinote(w->midi_note), w->key, comma,
                        (int)w->num_frames, (int)w->sample_rate, w->channels);
            }
            fprintf(f, "  }");
        } else {
            fprintf(f, "  \"%s\": [\n", s->name);
            for (int j = 0; j < stbds_arrlen(s->wave_indices); j++) {
                wave_t *w = &G->waves[s->wave_indices[j]];
                char comma = j == stbds_arrlen(s->midi_notes) - 1 ? ' ' : ',';
                fprintf(f, "      \"%s\"%c // %d %dhz %dch\n", w->key, comma, (int)w->num_frames, (int)w->sample_rate, w->channels);
            }
            fprintf(f, "    ]");
        }
    }
    fprintf(f, "\n}\n");
    fclose(f);
}

stereo sample_wave(voice_state_t *v, hap_t *h, wave_t *w, bool *at_end) {
    if (v->retrig) {
        //printf("RETRIG\n");
        v->phase = 0.f;
        v->grainphase = 0.f;
        v->grainheads[0] = 0.;
        v->grainheads[1] = 0.;
        v->grainheads[2] = 0.;
    }
    if (!w) {if (at_end) *at_end = true; return {0.f, 0.f};}
    if (!w->frames || w->download_in_progress) {if (at_end) *at_end = !w->download_in_progress; return {0.f, 0.f};}
    float stretch = h->get_param(P_TIMESTRETCH, 1.f);
    int numgrainheads = (stretch != 1.f) ? 3 : 1;
    double posmul = w->sample_rate / SAMPLE_RATE;
    float fromt = h->get_param(P_FROM, 0.f);
    float tot = h->get_param(P_TO, 1.f);
    #ifdef FLUX
    if (fromt >= -10.f && fromt < -9.f) 
        fromt = w->invflux[(int)((fromt+10.f) * 255.f)];
    if (tot >= -10.f && tot < -9.f) 
        tot = w->invflux[(int)((tot+10.f) * 255.f)];
    #else
    if (fromt >= -10.f && fromt < -9.f) fromt = frac(fromt);
    if (tot >= -10.f && tot < -9.f) tot = frac(tot);
    #endif
    float loops = h->get_param(P_LOOPS, 0.f);
    float loope = h->get_param(P_LOOPE, 0.f);
    int nsamps = w->num_frames * abs(tot-fromt);
    if (nsamps<=0) {if (at_end) *at_end = true; return {0.f, 0.f};}
    loops *= nsamps;
    loope *= nsamps;
            
    int firstsamp = w->num_frames * (min(fromt, tot));
    int numatend = 0;
    stereo au = {0.f, 0.f};
    float grainphase_cos = -1.f, grainphase_sin = 0.f;
    if (numgrainheads > 1) {
        grainphase_cos = cosf(v->grainphase);
        grainphase_sin = sinf(v->grainphase);
        v->grainphase += G->dt*(16.f*TAU*2.f/3.f) / max(1e-7f, h->get_param(P_GRAIN_SIZE, 1.f));
    }
    for (int h = 0; h < numgrainheads; h++) {
        float grain_level = grainphase_cos; // temporarily back this up....
        const static float c120 =  -0.5f, s120 = SQRT3 * 0.5f;
        // rotate the grainphase by 120 degrees
        grainphase_cos = grainphase_cos * c120 - grainphase_sin * s120;
        grainphase_sin = grainphase_sin * c120 + grain_level * s120;
        // set the grain level
        grain_level =0.33333f - grain_level * 0.33333f;
        double pos = v->grainheads[h] * posmul;
        v->grainheads[h] += v->dphase;
        if (loops < loope && pos > loops) {
            pos = fmodf(pos - loops, loope - loops) + loops;
            v->grainheads[h] = pos / posmul;
        } else if (pos >= nsamps) {
            numatend++;
            continue;
        }
        if (fromt>tot) pos=nsamps-pos;
        int i = (int)floorf(pos);
        float t = pos - (float)i;
        i+=firstsamp;
        i %= w->num_frames;
        if (i<0) i+=w->num_frames;
        bool wrap = (i==w->num_frames-1);
        if (w->channels>1) {
            i*=w->channels;
            stereo s0 = stereo{w->frames[i + 0], w->frames[i + 1]};
            i=wrap ? i : i+w->channels;
            stereo s1 = stereo{w->frames[i + 0], w->frames[i + 1]};
            au += (s0 + (s1 - s0) * t) * grain_level;
        } else {
            float s0 = w->frames[i + 0];
            i=wrap ? i : i+1;
            float s1 = w->frames[i + 0];
            s0 += (s1-s0)*t;
            s0 *= grain_level;
            au += stereo{s0,s0};
        }
    }
    if (at_end) *at_end = numatend == numgrainheads;
    v->phase+=v->dphase * stretch;
    double pos = v->phase * posmul;
    if (loops < loope && pos > loops) {
        pos = fmodf(pos - loops, loope - loops) + loops;
        v->phase = pos / posmul;
    }


    if (numgrainheads > 1) {
        if (v->grainphase > TAU/3.f) {
            v->grainphase -= TAU/3.f;
            float jitter = nsamps * h->get_param(P_JITTER, 0.f);
            v->grainheads[2] = v->grainheads[1];
            v->grainheads[1] = v->grainheads[0];
            v->grainheads[0] = v->phase + rnd01()*jitter;
        }
    }
    return au;
}



static inline stereo sample_saw(voice_state_t *v, hap_t *h, wave_t *w, bool *at_end) {
    // nb we DONT retrig the phase, and let the oscillator freerun to prevent clicks.
    double phase = v->phase * P_C3;
    v->phase += v->dphase;
    float wavetable_number = h->get_param(P_NUMBER, 0.f);
    return mono2st(shapeo(phase, v->dphase * P_C3, wavetable_number));
}

static inline stereo sample_supersaw(voice_state_t *v, hap_t *h, wave_t *w, bool *at_end) {
    // nb we DONT retrig the phase, and let the oscillator freerun to prevent clicks.
    float wavetable_number = h->get_param(P_NUMBER, 0.f);
    const static float _F0 = P_C3;
    const static float delta = 1.00174f;
    const static float _F1 = _F0 * 1.00175f;
    const static float _F2 = _F0 / 1.00137f;
    const static float _F3 = _F1 * 1.00159f;
    const static float _F4 = _F2 / 1.00143f;
    float s0 = shapeo(v->phase * _F0, v->dphase * _F0, wavetable_number);
    float s1 = shapeo(v->phase * _F1, v->dphase * _F1, wavetable_number);
    float s2 = shapeo(v->phase * _F2, v->dphase * _F2, wavetable_number);
    float s3 = shapeo(v->phase * _F3, v->dphase * _F3, wavetable_number);
    float s4 = shapeo(v->phase * _F4, v->dphase * _F4, wavetable_number);
    float l = s0 * 0.5 + s1 * 0.4 + s2 * 0.6 + s3 * 0.7 + s4 * 0.3;
    float r = s0 * 0.5 + s1 * 0.6 + s2 * 0.4 + s3 * 0.3 + s4 * 0.7;
    v->phase += v->dphase;
    return stereo{l, r};
}



int init_sampler(bool eager) {
    ensure_curl_global();
    // add sounds for 'rest'.
    if (stbds_hmlen(G->sounds) == 0) {
        get_sound_init_only("~");
        get_sound_init_only("1");
        register_osc("_wave", sample_wave);
        register_osc("saw", sample_saw);
        register_osc("supersaw", sample_supersaw);
    }

    bool prefer_offline = !eager;
    parse_strudel_json("github:mot4i/loom/main/a_damn_fine_cup_of_coffee", prefer_offline);

#define DS "https://raw.githubusercontent.com/felixroos/dough-samples/main/"
#define TS "https://raw.githubusercontent.com/todepond/samples/main/"
    // bd seems to be in here...
    parse_strudel_json("github:tidalcycles/uzu-drumkit", prefer_offline, NULL);
    parse_strudel_json("github:tidalcycles/Dirt-Samples/master", prefer_offline, NULL); // 0e6d60a72c916a2ec5161d02afae40ccd6ea7a91
    // parse_strudel_json(DS "Dirt-Samples/strudel.json");
    parse_strudel_json(DS "tidal-drum-machines.json", prefer_offline, NULL);
    parse_strudel_json(DS "piano.json", prefer_offline, NULL);
    // parse_strudel_json(DS "Dirt-Samples.json");
    parse_strudel_json(DS "EmuSP12.json", prefer_offline, "emusp12_");
    // parse_strudel_json(DS "uzu-drumkit.json"); 404 not found?
    parse_strudel_json(DS "vcsl.json", prefer_offline, NULL);
    parse_strudel_json(DS "mridangam.json", prefer_offline, NULL);
    parse_strudel_json("github:yaxu/clean-breaks", prefer_offline, "break_");
    parse_strudel_json("github:switchangel/breaks", prefer_offline, "switchangel_");
    parse_strudel_json("github:switchangel/pad", prefer_offline, "switchangel_");

    parse_strudel_json("https://samples.grbt.com.au/strudel.json", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/Breaks", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/Riffs_Arps_Hits", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/FX", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/Pads", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/Vocals", prefer_offline);
    parse_strudel_json("github:sonidosingapura/blu-mar-ten/main/Bass", prefer_offline);
    parse_strudel_json("github:yaxu/spicule", prefer_offline);
    parse_strudel_json("github:mot4i/garden", prefer_offline);

    parse_strudel_json("github:plinkysynth/ginkgo_samples/main/microlive", prefer_offline);


    //parse_strudel_json("file://samples/junglejungle/strudel.json", NULL);
    parse_strudel_alias_json(TS "tidal-drum-machines-alias.json", prefer_offline);
    printf(COLOR_YELLOW "%d" COLOR_RESET " sounds registered, " COLOR_GREEN "%d" COLOR_RESET " waves.\n", num_sounds(),
           num_waves());
    dump_all_sounds("allsounds.json");
    if (eager) {
        printf(COLOR_RED "===========================================" COLOR_RESET "\n");
        printf(COLOR_RED "prefetching all sample assets\n" COLOR_RESET "\n");
        printf(COLOR_RED "===========================================" COLOR_RESET "\n");
    
        for (int i = 0; i < stbds_shlen(G->waves); i++) {
            load_wave_now(&G->waves[i]);
        }

    }
    return 0;
}

void pump_wave_load_requests_main_thread(void) {
    if (!G)
        return;
    while (1) {
        spin_lock(&G->load_request_cs);
        int n = stbds_hmlen(G->load_requests);
        int i;
        for (i = 0; i < n; i++)
            if (G->load_requests[i].key->download_in_progress<2)
                break;
        wave_t *req = (i < n) ? G->load_requests[i].key : nullptr;
        spin_unlock(&G->load_request_cs);
        if (i >= n)
            return;
        if (req) {
            load_wave_now(req);
        }
    }
}
