#pragma once
// nicer api for 3rdparty/sj.h
// tldr:
//
// sj_Reader r = read_json_file("settings.json"); // stb_ds string
// for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
//     if (iter_key_is(&outer, "camera")) {
//         for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
//             .... val_as_float ....
//         }
//     }
// }
// free_json(&r);

#include "3rdparty/sj.h"

typedef struct sj_iter_t {
    sj_Reader *r;
    sj_Value outer;
    sj_Value key;
    sj_Value val;
} sj_iter_t;


static inline sj_Reader read_json_file(const char *fname) {
    char *str = load_file(fname);
    return sj_reader(str, stbds_arrlen(str));
}

static inline sj_iter_t iter_start(sj_Reader *r, sj_Value *parent = NULL) { // parent is optional
    return (sj_iter_t){.r = r, .outer = parent ? *parent : sj_read(r)};
}

static inline bool iter_next(sj_iter_t *iter) {
    if (iter->outer.type == SJ_OBJECT) {
        return sj_iter_object(iter->r, iter->outer, &iter->key, &iter->val);
    } else if (iter->outer.type == SJ_ARRAY) {
        return sj_iter_array(iter->r, iter->outer, &iter->val);
    } else {
        return false;
    }
}

static inline bool iter_key_is(sj_iter_t *iter, const char *key) {
    return spancmp(iter->key.start, iter->key.end, key, NULL) == 0;
}

static inline float iter_val_as_float(sj_iter_t *iter, float default_val=0.f) {
    if (iter->val.type != SJ_NUMBER) return default_val;
    return atof(temp_cstring_from_span(iter->val.start, iter->val.end));
}

static inline int iter_val_as_int(sj_iter_t *iter, int default_val=0) {
    if (iter->val.type != SJ_NUMBER) return default_val;
    return atoi(temp_cstring_from_span(iter->val.start, iter->val.end));
}

#ifdef _EXT_VECTOR_H
static inline float4 iter_val_as_float4(sj_iter_t *iter, float4 default_val={}) {
    if (iter->val.type != SJ_ARRAY) return default_val;
    float4 rv = default_val;
    sj_iter_t it = iter_start(iter->r, &iter->val);
    for (int i=0;i<4 && iter_next(&it);i++) {
        rv[i] = iter_val_as_float(&it, rv[i]);
    }
    return rv;
}
#endif

static inline char *iter_val_as_stbstring(sj_iter_t *iter, const char *default_val=NULL) {
    if (iter->val.type != SJ_STRING) return (char*)default_val;
    return stbstring_from_span(iter->val.start, iter->val.end, 0);
}

static inline void free_json(sj_Reader *r) {
    if (r)stbds_arrfree(r->data);
}
