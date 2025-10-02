#define SJ_IMPL
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "3rdparty/stb_ds.h"
#include "3rdparty/sj.h"
#include "3rdparty/miniaudio.h"
#include "ginkgo.h"
#include "utils.h"
#include "miniparse.h"
#include "sampler.h"
#include "ansicols.h"

const char *fetch_to_cache(const char *url, int prefer_offline); // from http_fetch.c

bool decode_file_to_f32(const char *path, wave_t *out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000); // keep source ch/sr
    float *pcm=NULL;
    uint64_t num_frames=0;
    ma_result rc = ma_decode_file(path, &cfg, &num_frames, (void**) &pcm);
    if (rc!=MA_SUCCESS || num_frames==0) {
        free(pcm);
        return false;
    }
    out->num_frames = num_frames;
    out->sample_rate = cfg.sampleRate;
    out->channels = cfg.channels;
    out->frames = pcm;
    printf("read %lld frames %dhz from %s\n", out->num_frames,(int)out->sample_rate, path);
    return true;
}

static float _silence[16] = {};

void load_wave_now(wave_t *wave) {
    if (wave->frames) return ; // already loaded
    const char *fname = fetch_to_cache(wave->url, 1);
    if (fname) {
        decode_file_to_f32(fname, wave);
    }
    if (!wave->frames) {
        fprintf(stderr,"warning: wave %s failed to load. replacing with silence\n", wave->url);
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
    }
    else if (obj.type == SJ_OBJECT)
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
    while (sj_iter_object(&r, outer_obj, &soundkey, &soundval)) {
        if (soundval.type == SJ_STRING) {
            char *long_name = temp_cstring_from_span(soundkey.start, soundkey.end);
            char *short_name = temp_cstring_from_span(soundval.start, soundval.end);
            //printf("alias %s -> %s\n", long_name, short_name);
            add_alias_init_only(short_name, long_name);
        } else {
            fprintf(stderr, "alias json warning: unexpected object type: %.*s %d\n", (int)(soundkey.end - soundkey.start), soundkey.start, soundval.type);
        }
    }
    printf("after " COLOR_YELLOW "%s" COLOR_RESET ", %d sounds\n", json_url, num_sounds());
    stbds_arrfree(json);
    return 1;
}
int parse_strudel_json(const char *json_url) {
    const char *json_fname = fetch_to_cache(json_url, 1);
    char *json = load_file(json_fname);
    sj_Reader r = sj_reader(json, stbds_arrlen(json));
    sj_Value outer_obj = sj_read(&r);
    sj_Value soundval, soundkey;
    sj_Value base = {};
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
            char *sound_name = temp_cstring_from_span(soundkey.start, soundkey.end);
            Sound *sound =get_sound_init_only(sound_name);
                sj_Value samplekey, sample;
            for (int i = 0; sj_iter_array_or_object(&r, soundval, &samplekey, &sample); i++) {
                char *url = NULL;
                int urllen = sample.end - sample.start + base.end - base.start + 1;
                stbds_arrsetlen(url, urllen);
                snprintf(url, urllen, "%.*s%.*s", (int)(base.end - base.start), base.start, (int)(sample.end - sample.start),
                         sample.start);
                
                int midinote = parse_midinote(samplekey.start, samplekey.end, false);
                //printf("%.*s (%d) -> %s\n", (int)(samplekey.end-samplekey.start), samplekey.start, midinote, url);
                int j;
                for (j = 0; j< stbds_arrlen(sound->waves); ++j) if (strcmp(sound->waves[j].url, url)==0) break;
                if (j==stbds_arrlen(sound->waves)) {
                    wave_t wave={.url = url};
                    bool eager=false;
                    if (eager) 
                        load_wave_now(&wave);                        
                    stbds_arrput(sound->waves, wave); // TODO; if this causes sound->waves to grow, we may get an exception on the other thread...
                    if (midinote>=0) {
                        int_pair_t pair = {.k = midinote, .v = j};
                        int insert_at = lower_bound_int_pair(sound->midi_notes, stbds_arrlen(sound->midi_notes), pair);
                        stbds_arrins(sound->midi_notes, insert_at, pair);
                    }
                }
            }
            // log the number of waves in this sound
            // printf("%s -> %d waves\n", sound_name, (int)stbds_arrlen(sound->waves));
        }
    }
    printf("after " COLOR_YELLOW "%s" COLOR_RESET ", %d sounds\n", json_url, num_sounds());
    stbds_arrfree(json);
    return 1;
}

int init_sampler(void) {
#define DS "https://raw.githubusercontent.com/felixroos/dough-samples/main/"
#define TS "https://raw.githubusercontent.com/todepond/samples/main/"
// bd seems to be in here...
    parse_strudel_json("https://raw.githubusercontent.com/tidalcycles/uzu-drumkit/refs/heads/main/strudel.json");
    parse_strudel_json("https://raw.githubusercontent.com/tidalcycles/Dirt-Samples/master/strudel.json"); // 0e6d60a72c916a2ec5161d02afae40ccd6ea7a91
    //parse_strudel_json(DS "Dirt-Samples/strudel.json");
    parse_strudel_json(DS "tidal-drum-machines.json");
    parse_strudel_json(DS "piano.json");
    parse_strudel_json(DS "Dirt-Samples.json");
    parse_strudel_json(DS "EmuSP12.json");
    // parse_strudel_json(DS "uzu-drumkit.json"); 404 not found?
    parse_strudel_json(DS "vcsl.json");
    parse_strudel_json(DS "mridangam.json");
    parse_strudel_alias_json(TS "tidal-drum-machines-alias.json");
    return 0;
}

void pump_wave_load_requests_main_thread(void) {
    if (!G) return;
    while (1) {
        spin_lock(&G->load_request_cs);
        int n = stbds_hmlen(G->load_requests);
        sound_request_t *req = n ? &G->load_requests[0] : NULL;
        spin_unlock(&G->load_request_cs);
        if (!n) return;
        if (req) {
            load_wave_now(&req->key.sound->waves[req->key.index]);
            spin_lock(&G->load_request_cs);
            stbds_hmdel(G->load_requests, req->key);
            spin_unlock(&G->load_request_cs);
        }
    }
}
