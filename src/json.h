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
        if (iter->val.start==NULL) {
            // yield a straight value once.
            iter->val = iter->outer;
            return true;
        }
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

static inline unsigned int iter_val_as_uint(sj_iter_t *iter, int default_val=0) {
    if (iter->val.type != SJ_NUMBER) return default_val;
    return strtoul(temp_cstring_from_span(iter->val.start, iter->val.end), 0, 10);
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

static inline char *iter_key_as_stbstring(sj_iter_t *iter) {
    return stbstring_from_span(iter->key.start, iter->key.end, 0);
}


static inline char *iter_val_as_stbstring(sj_iter_t *iter, const char *default_val=NULL) {
    if (iter->val.type != SJ_STRING) return (char*)default_val;
    return stbstring_from_span(iter->val.start, iter->val.end, 0);
}

static inline void free_json(sj_Reader *r) {
    if (r)stbds_arrfree(r->data);
}

typedef struct json_printer_t {
    FILE *f;
    int indent;
    uint32_t is_object;
    bool trailing_comma;
} json_printer_t;

static inline void json_print_trailing_comma_and_key(json_printer_t *jp, const char *key) {
    if (jp->trailing_comma) fputc(',', jp->f);
    jp->trailing_comma = true;
    bool is_array = !(jp->is_object & 1);
    if (is_array) {
        fprintf(jp->f, "\n%*s", jp->indent*2, "");
    } else {
        fprintf(jp->f, "\n%*s\"%s\": ", jp->indent*2, "", key);
    }
}

static inline void json_print(json_printer_t *jp, const char *key, float val) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "%f", val);
}

static inline void json_print(json_printer_t *jp, const char *key, int val) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "%d", val);
}

static inline void json_print(json_printer_t *jp, const char *key, unsigned int val) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "%u", val);
}

static inline void json_print(json_printer_t *jp, const char *key, const char *val, const char *e = 0) {
    json_print_trailing_comma_and_key(jp, key);
    fputc('"', jp->f);
    if (!e && val) e = val + strlen(val);
    for (const char *c=val; c<e; c++) {
        char ch = *c;
        switch (ch) {
            case '"':
            case '\\':
                fputc('\\', jp->f);
                fputc(ch, jp->f);
                break;
            case '\n':
                fputc('\\', jp->f);
                fputc('n', jp->f);
                break;
            case '\r':
                fputc('\\', jp->f);
                fputc('r', jp->f);
                break;
            case '\t':
                fputc('\\', jp->f);
                fputc('t', jp->f);
                break;  
            case '\b':
                fputc('\\', jp->f);
                fputc('b', jp->f);
                break;
            case '\f':
                fputc('\\', jp->f);
                fputc('f', jp->f);
                break;
            default:
            // TODO utf8
                if (ch < 32 || ch > 126) {
                    fprintf(jp->f, "\\u%04x", ch);
                } else {
                    fputc(ch, jp->f);
                }
                break;
        }
    }
    fputc('"', jp->f);
}

static inline void json_print(json_printer_t *jp, const char *key, bool val) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "%s", val ? "true" : "false");
}

#ifdef _EXT_VECTOR_H
static inline void json_print(json_printer_t *jp, const char *key, float4 val) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "[%f, %f, %f, %f]", val.x, val.y, val.z, val.w);
}
#endif

static inline void json_start_object(json_printer_t *jp, const char *key) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "{");
    jp->indent++;
    jp->is_object = (jp->is_object)*2 + 1;
    jp->trailing_comma = false;
}

static inline void json_start_array(json_printer_t *jp, const char *key) {
    json_print_trailing_comma_and_key(jp, key);
    fprintf(jp->f, "[");
    jp->indent++;
    jp->is_object = (jp->is_object)*2;
    jp->trailing_comma = false;
}

static inline void json_end_object(json_printer_t *jp) {
    if (jp->indent) {
        jp->indent--;
        bool is_array = !(jp->is_object & 1);
        fprintf(jp->f, "\n%*s%c", jp->indent*2, "", is_array ? ']' : '}');
        jp->is_object>>=1;
        jp->trailing_comma = true;
    }
}

static inline void json_end_array(json_printer_t *jp) {
    json_end_object(jp);
}

static inline void json_end_file(json_printer_t *jp) {
    while (jp->indent) {
        json_end_object(jp);
    }
    fprintf(jp->f, "\n");
    fclose(jp->f);
}
