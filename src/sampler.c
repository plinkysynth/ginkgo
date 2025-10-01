#define SJ_IMPL
#include <stdio.h>
#include <stdbool.h>
#include "3rdparty/stb_ds.h"
#include "ginkgo.h"
#include "utils.h"
#include "sampler.h"
#include "wavfile.h"
#include "3rdparty/sj.h"

const char*fetch_to_cache(const char *url, int prefer_offline);

int parse_strudel_json(const char *json_url) {
    const char *json_fname = fetch_to_cache(json_url, 1);
    char *json = load_file(json_fname);
    sj_Reader r = sj_reader(json, stbds_arrlen(json));
    sj_Value outer_obj = sj_read(&r);
    sj_Value soundval, soundkey;
    sj_Value base={};
    while (sj_iter_object(&r, outer_obj, &soundkey, &soundval)) {
        if (soundval.type == SJ_ARRAY) {
            sj_Value sample;
            for (int i = 0; sj_iter_array(&r, soundval, &sample); i++) {
                //printf("%.*s:%d = '%.*s'\n", (int)(soundkey.end-soundkey.start), soundkey.start, i, (int)(sample.end-sample.start), sample.start);
            }
        } else if (soundval.type == SJ_OBJECT) {
            sj_Value samplekey, sample;
            for (int i = 0; sj_iter_object(&r, soundval, &samplekey, &sample); i++) {
                char *url = NULL;
                int urllen = sample.end-sample.start + base.end-base.start + 1;
                stbds_arrsetlen(url, urllen);
                snprintf(url, urllen, "%.*s%.*s", (int)(base.end-base.start), base.start, (int)(sample.end-sample.start), sample.start);
                printf("%s\n", url);
                const char *fname = fetch_to_cache(url, 1);
                stbds_arrfree(url);
                //break;
            }

        } else {
            if (spancmp(soundkey.start, soundkey.end, "_base", NULL) == 0) {
                //printf("base is %.*s\n", (int)(soundval.end-soundval.start), soundval.start);
                base=soundval;
            } else {
                printf("json warning: unexpected object type: %.*s %d\n", (int)(soundkey.end-soundkey.start), soundval.start, soundval.type);
            }
        }
    }
    printf("read samples from %s\n", json_url);
    stbds_arrfree(json);
    return 1;
}

int init_sampler(void) {
    #define DS "https://raw.githubusercontent.com/felixroos/dough-samples/main/"
    #define TS "https://raw.githubusercontent.com/todepond/samples/main/"

 /*
    strudel gets its sample packs from: gm.mjs and then

  const ds = 'https://raw.githubusercontent.com/felixroos/dough-samples/main/';
  const ts = 'https://raw.githubusercontent.com/todepond/samples/main/';
    samples(`${ds}/tidal-drum-machines.json`),
    samples(`${ds}/piano.json`),
    samples(`${ds}/Dirt-Samples.json`),
    samples(`${ds}/uzu-drumkit.json`),
    samples(`${ds}/vcsl.json`),
    samples(`${ds}/mridangam.json`),
  aliasBank(`${ts}/tidal-drum-machines-alias.json`);
  */
  //parse_strudel_json(DS "Dirt-Samples.json");
  parse_strudel_json(DS "piano.json");
    return 0;

}