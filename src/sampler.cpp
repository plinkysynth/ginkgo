#define SJ_IMPL
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <ctype.h>
#include <string.h>
#include "3rdparty/stb_ds.h"
#include "3rdparty/sj.h"
#include "3rdparty/miniaudio.h"
#include "ginkgo.h"
#include "utils.h"
#include "miniparse.h"
#include "sampler.h"
#include "ansicols.h"

typedef void (*http_fetch_callback_t)(const char *url, const char *fname, void *userdata);
const char *fetch_to_cache(const char *url, int prefer_offline, http_fetch_callback_t callback=0, void *userdata=0); // from http_fetch.c

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

bool decode_file_to_f32(const char *path, wave_t *out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // keep source ch/sr
    float *pcm = NULL;
    uint64_t num_frames = 0;
    ma_result rc = ma_decode_file(path, &cfg, &num_frames, (void **)&pcm);
    if (rc != MA_SUCCESS || num_frames == 0) {
        free(pcm);
        return false;
    }
    out->num_frames = num_frames;
    out->sample_rate = cfg.sampleRate;
    out->channels = cfg.channels;
    out->frames = pcm;
    const char *trimmed_path = strrchr(path, '/');
    // printf("read %lld samples @ %dkhz %dchannels from " COLOR_YELLOW "%s" COLOR_RESET "\n", out->num_frames,
    //        (int)out->sample_rate / 1000, out->channels, trimmed_path ? trimmed_path + 1 : path);
    return true;
}

static float _silence[16] = {};

static void wave_loaded(const char *url, const char *fname, void *userdata) {
}

void load_wave_now(Sound *sound, wave_t *wave) {
    if (wave->frames)
        return; // already loaded
    const char *fname = fetch_to_cache(wave->url, 1, wave_loaded, sound);
    if (fname) {
        decode_file_to_f32(fname, wave);
    }
    if (!wave->frames) {
        fprintf(stderr, "warning: wave %s failed to load. replacing with silence\n", wave->url);
        wave->num_frames = 1;
        wave->sample_rate = 48000;
        wave->channels = 1;
        wave->frames = _silence;
    }
}

bool sj_iter_array_or_object(sj_Reader *r, sj_Value obj, sj_Value *key, sj_Value *val) {
    if (obj.type == SJ_ARRAY) {
        *key = (sj_Value){};
        return sj_iter_array(r, obj, val);
    } else if (obj.type == SJ_OBJECT)
        return sj_iter_object(r, obj, key, val);
    else
        return false;
}

int parse_strudel_alias_json(const char *json_url) {
    const char *json_fname = fetch_to_cache(json_url, 1);
    char *json = load_file(json_fname);
    sj_Reader r = sj_reader(json, stbds_arrlen(json));
    sj_Value outer_obj = sj_read(&r);
    sj_Value soundval, soundkey;
    int old_num_sounds = num_sounds();
    while (sj_iter_object(&r, outer_obj, &soundkey, &soundval)) {
        if (soundval.type == SJ_STRING) {
            char *long_name = temp_cstring_from_span(soundkey.start, soundkey.end);
            char *short_name = temp_cstring_from_span(soundval.start, soundval.end);
            // printf("alias %s -> %s\n", long_name, short_name);
            add_alias_init_only(short_name, long_name);
        } else {
            fprintf(stderr, "alias json warning: unexpected object type: %.*s %d\n", (int)(soundkey.end - soundkey.start),
                    soundkey.start, soundval.type);
        }
    }
    printf(COLOR_YELLOW "%s" COLOR_RESET " added " COLOR_GREEN "%d" COLOR_RESET " new aliases\n", trimurl(json_url),
           num_sounds() - old_num_sounds);
    stbds_arrfree(json);
    return 1;
}

static inline bool char_needs_escaping(const char *s, const char *e) {
    if (*s=='%' && (s+1<e) && s[1]=='_') return true;
    return *s==' ' || *s=='#';
    //return !(isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c=='/');
}
static inline int get_url_length_after_escaping(const char *s, const char *e) {
    int len = e - s;
    for (; s < e; s++)
        if (char_needs_escaping(s,e))
            len += 2;
    return len;
}

static inline const char *escape_url(char *dstbuf, const char *s, const char *e) {
    const static char hex[17] = "0123456789abcdef";
    char *d = dstbuf;
    for (; s < e; s++) {
        if (char_needs_escaping(s,e)) {
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

int parse_strudel_json(const char *json_url, const char *sound_prefix, bool eager) {
    int prefix_len = sound_prefix ? strlen(sound_prefix) : 0;
    const char *json_fname = fetch_to_cache(json_url, 1);
    char *json = load_file(json_fname);
    sj_Reader r = sj_reader(json, stbds_arrlen(json));
    sj_Value outer_obj = sj_read(&r);
    sj_Value soundval, soundkey;
    sj_Value base = {};
    int old_num_sounds = num_sounds();
    int wav_count = 0, skip_count = 0;
    while (sj_iter_object(&r, outer_obj, &soundkey, &soundval)) {
        if (soundval.type == SJ_STRING) {
            if (spancmp(soundkey.start, soundkey.end, "_base", NULL) == 0) {
                // printf("base is %.*s\n", (int)(soundval.end-soundval.start), soundval.start);
                base = soundval;
            } else {
                printf("json warning: unexpected object type: %.*s %d\n", (int)(soundkey.end - soundkey.start), soundval.start,
                       soundval.type);
            }
        } else {
            int sound_name_len = (soundkey.end - soundkey.start) + prefix_len;
            char sound_name[sound_name_len + 1];
            memcpy(sound_name, sound_prefix, prefix_len);
            memcpy(sound_name + prefix_len, soundkey.start, soundkey.end - soundkey.start);
            sound_name[sound_name_len] = 0;
            Sound *sound = get_sound_init_only(sound_name);
            sj_Value samplekey, sample;
            for (int i = 0; sj_iter_array_or_object(&r, soundval, &samplekey, &sample); i++) {
                char *url = NULL;
                int numbase = base.end - base.start;
                int numurl = get_url_length_after_escaping(sample.start, sample.end);
                stbds_arrsetlen(url, numbase + numurl + 1);
                memcpy(url, base.start, numbase);
                url[numbase] = 0;
                if (strncmp(url, "file://", 7) == 0 || strstr(url, "://") == 0) {
                    memcpy(url + numbase, sample.start, sample.end - sample.start);
                    url[numbase + (sample.end - sample.start)] = 0;
                } else {
                    escape_url(url + numbase, sample.start, sample.end);
                }
                int midinote = parse_midinote(samplekey.start, samplekey.end, NULL, false);
                // printf("%.*s (%d) -> %s\n", (int)(samplekey.end-samplekey.start), samplekey.start, midinote, url);
                int j;
                for (j = 0; j < stbds_arrlen(sound->waves); ++j)
                    if (strcmp(sound->waves[j].url, url) == 0)
                        break;
                if (j == stbds_arrlen(sound->waves)) {
                    wav_count++;
                    wave_t wave = {.url = url, .midi_note = midinote};
                    if (eager)
                        load_wave_now(sound, &wave);
                    stbds_arrput(sound->waves,
                                 wave); // TODO; if this causes sound->waves to grow, we may get an exception on the other thread...
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
        }
    }
    printf(COLOR_YELLOW "%s" COLOR_RESET ", %d new sounds, added " COLOR_GREEN "%d" COLOR_RESET " waves\n", trimurl(json_url),
           num_sounds() - old_num_sounds, wav_count);
    stbds_arrfree(json);
    return 1;
}

int num_waves(void) {
    int n = num_sounds();
    int numwaves = 0;
    for (int i = 0; i < n; ++i)
        numwaves += stbds_arrlen(G->sounds[i].value->waves);
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
                wave_t *w = &s->waves[s->midi_notes[j].v];
                char comma = j == stbds_arrlen(s->midi_notes) - 1 ? ' ' : ',';
                //printf("midi note %s -> %s\n", print_midinote(w->midi_note), w->url);
                fprintf(f, "    \"%s\": \"%s\"%c // %d %dhz %dch\n", print_midinote(w->midi_note), w->url, comma, (int)w->num_frames,
                        (int)w->sample_rate, w->channels);
            }
            fprintf(f, "  }");
        } else {
            fprintf(f, "  \"%s\": [\n", s->name);
            for (int j = 0; j < stbds_arrlen(s->waves); j++) {
                wave_t *w = &s->waves[j];
                char comma = j == stbds_arrlen(s->midi_notes) - 1 ? ' ' : ',';
                fprintf(f, "      \"%s\"%c // %d %dhz %dch\n", w->url, comma, (int)w->num_frames, (int)w->sample_rate, w->channels);
            }
            fprintf(f, "    ]");
        }
    }
    fprintf(f, "\n}\n");
    fclose(f);
}

int init_sampler(void) {
    // add sounds for 'rest'.
    if (stbds_hmlen(G->sounds) == 0) {
        get_sound_init_only("~");
        get_sound_init_only("1");
    }

    bool eager = false;
#define DS "https://raw.githubusercontent.com/felixroos/dough-samples/main/"
#define TS "https://raw.githubusercontent.com/todepond/samples/main/"
    // bd seems to be in here...
    parse_strudel_json("https://raw.githubusercontent.com/tidalcycles/uzu-drumkit/refs/heads/main/strudel.json", NULL, eager);
    parse_strudel_json("https://raw.githubusercontent.com/tidalcycles/Dirt-Samples/master/strudel.json", NULL,
                       eager); // 0e6d60a72c916a2ec5161d02afae40ccd6ea7a91
    // parse_strudel_json(DS "Dirt-Samples/strudel.json");
    parse_strudel_json(DS "tidal-drum-machines.json", NULL, eager);
    parse_strudel_json(DS "piano.json", NULL, eager);
    // parse_strudel_json(DS "Dirt-Samples.json");
    parse_strudel_json(DS "EmuSP12.json", NULL, eager);
    // parse_strudel_json(DS "uzu-drumkit.json"); 404 not found?
    parse_strudel_json(DS "vcsl.json", NULL, eager);
    parse_strudel_json(DS "mridangam.json", NULL, eager);
    parse_strudel_json("https://raw.githubusercontent.com/yaxu/clean-breaks/main/strudel.json", "break_", eager);
    parse_strudel_json("https://raw.githubusercontent.com/switchangel/breaks/main/strudel.json", "switchangel_", eager);
    parse_strudel_json("https://raw.githubusercontent.com/switchangel/pad/main/strudel.json", "switchangel_", eager);
    parse_strudel_json("file://samples/junglejungle/strudel.json", NULL, eager);
    parse_strudel_alias_json(TS "tidal-drum-machines-alias.json");
    printf(COLOR_YELLOW "%d" COLOR_RESET " sounds registered, " COLOR_GREEN "%d" COLOR_RESET " waves.\n", num_sounds(),
           num_waves());
    dump_all_sounds("allsounds.json");
    return 0;
}

void pump_wave_load_requests_main_thread(void) {
    if (!G)
        return;
    while (1) {
        spin_lock(&G->load_request_cs);
        int n = stbds_hmlen(G->load_requests);
        int i;
        for (i = 0; i < n; i++) if (!G->load_requests[i].download_in_progress) break;
        sound_request_t *req = (i<n) ? &G->load_requests[i] : NULL;
        spin_unlock(&G->load_request_cs);
        if (i>=n)
            return;
        if (req) {
            // TODO: add multi download with curl, and move the lines after 'load wave now' into the done-callback.
            req->download_in_progress = 1;
            load_wave_now(req->key.sound, &req->key.sound->waves[req->key.index]);
            req->download_in_progress = 0;
            spin_lock(&G->load_request_cs);
            stbds_hmdel(G->load_requests, req->key);
            spin_unlock(&G->load_request_cs);
        }
    }
}
