#pragma once

// init only ones are allowed to mutate the sound map.
Sound *get_sound_init_only(const char *name); // ...by name.
Sound *add_alias_init_only(const char *short_alias, const char *long_name);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);

const static float param_defaults[P_LAST] = {
#define X(x, shortname, def, ...) def,
#include "params.h"
};

enum EValueType : int8_t { // for Node.value_type
    VT_NONE = -1,
    VT_NUMBER = P_NUMBER,
    VT_NOTE = P_NOTE,
    VT_SOUND = P_SOUND,
    VT_SCALE = P_SCALEBITS,
};

typedef struct Node {
    uint8_t type;
    int8_t value_type;   // note, number, sound
    int32_t start, end;  // half-open [start,end) character range (for syntax hilighting)
    int linenumber;      // whate line we are on
    int32_t first_child; // index or -1
    int32_t next_sib;    // index or -1 - linked list of siblings
    int num_children;
    float total_length;         // total length of all children (nb elongated nodes are longer than 1)
    float min_value, max_value; // parsed value of the node
} Node;

void merge_hap(hap_t *dst, hap_t *src); // copy valid fields from src onto dst

typedef struct hap_span_t {
    hap_t *s, *e;
    inline int size() const { return (int)(e - s); }
    inline bool empty() const { return s >= e; }
    inline bool hasatleast(int i) const { return s + i < e; }
} hap_span_t;

typedef struct bfs_node_t {
    uint8_t type;
    int8_t value_type; // note, number, sound
    uint16_t num_children;
    int32_t first_child; // index or -1
} bfs_node_t;

typedef struct float_minmax_t {
    float mn, mx;
} float_minmax_t;

typedef struct token_info_t {
    int start, end;
    float last_evaled_glfw_time;
    float local_time_of_eval;
} token_info_t;

typedef struct shader_param_t {
    float value;
    float old_value;
    float integrated_value;
    float old_integrated_value;

    void update(float new_value, float dt, bool reset_integration) {
        old_value = value;
        value = new_value;
        old_integrated_value = integrated_value;
        if (reset_integration)
            old_integrated_value = integrated_value = 0.f;
        else
            integrated_value += value * dt;
    }
    void reset(void) {
        value = 0.f;
        old_value = 0.f;
        integrated_value = 0.f;
        old_integrated_value = 0.f;
    }
} shader_param_t;

typedef void (*fn_cb_t)(int left_hap_idx, hap_span_t &dst, int tmp_size, float viz_time, int num_src_haps, hap_t **srchaps,
                       int newid, size_t context, hap_time when);

typedef struct pattern_t { // a parsed version of a min notation string
    const char *key;
    uint32_t seed;
    float *curvedata; // stb_ds
    uint32_t colbitmask;
    float4 over_color;
    float x, y;
    float last_scale_root;
    uint32_t last_scale_bits;
    float get_near_output(pattern_t *other) const {
        if (!other)
            return 0.f;
        float dx = x - other->x;
        float dy = y - other->y;
        float dsq = (dx * dx + dy * dy);
        // max is 10 at a distance of 40 pixels.
        if (dsq < 40.f * 40.f)
            return 10.f;
        return 40.f * 40.f / dsq * 10.f;
    }
    float get_color_output(int c) const {
        static const float4 cols[8] = {
            {1, 1, 1, 0},    // white
            {1, 0, 0, 0},    // red
            {1, 0.81, 0, 0}, // yellow
            {0, 1, 0, 0},    // green
            {0, 1, 0.81, 0}, // cyan
            {0, 0, 1, 0},    // blue
            {0.81, 0, 1, 0}, // pink
            {0, 0, 0, 0},    // black
        };
        switch (c) {
        case 8:
            return x * (1.f / 1920.f);
        case 9:
            return 1.f - y * (1.f / 1080.f);
        case 0 ... 7: {
            if (over_color.w <= 0.f)
                return 0.f;
            float4 oc = over_color / over_color.w;
            oc.w = 0.f;
            float rv = 1.f - length(cols[c] - oc);
            if (rv < 0.f)
                return 0.f;
            return rv * over_color.w;
        }
        default:
            return 0.f;
        }
    }
    // stuff for shaders:
    int uniform_idx;
    shader_param_t shader_param;
    float picked;

    // bfs
    token_info_t *bfs_start_end;       // source code ranges
    float_minmax_t *bfs_min_max_value; // parsed value of the node
    float *bfs_grid_time_offset;
    float *bfs_kids_total_length;
    bfs_node_t *bfs_nodes;

    float get_length(int nodeidx);
    void _filter_haps(hap_span_t left_haps, hap_time speed_scale, hap_time tofs, hap_time when);
    void _apply_fn(int nodeidx, int hapid, hap_span_t &dst, int tmp_size, float viz_time, size_t context, hap_time when, fn_cb_t fn);
    hap_span_t _make_haps(hap_span_t &dst, int tmp_size, float viz_time, int nodeidx, hap_time when, int hapid);
    float compute_blendnear_weights(bfs_node_t *n, float *weights);
    bool _append_leaf_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid); // does random range
    bool _append_number_hap(hap_span_t &dst, int nodeidx, int hapid, float value);
    hap_span_t make_haps(hap_span_t dst, int tmp_size, float viz_time, hap_time when) {
        hap_span_t haps = _make_haps(dst, tmp_size, viz_time, 0, when, seed);
        return haps;
    }
    void unalloc() {
        stbds_arrfree(curvedata);
        stbds_arrfree(bfs_start_end);
        stbds_arrfree(bfs_min_max_value);
        stbds_arrfree(bfs_kids_total_length);
        stbds_arrfree(bfs_grid_time_offset);
        stbds_arrfree(bfs_nodes);
    }
} pattern_t;

typedef struct error_msg_t {
    int key;           // a line number
    const char *value; // a line of text (terminated by \n)
} error_msg_t;

typedef struct pattern_maker_t {
    Node *nodes;      // stb_ds
    float *curvedata; // stb_ds
    const char *s;    // ...the source string.
    int root;         // index of root node
    int32_t n;        // length of string
    int32_t i;        // current position in string during parsing.
    int linecount;    // counts \n
    int err;          // 0 ok, else position of first error
    int errline;
    const char *errmsg;
    pattern_t make_pattern(const char *key = NULL, int index_to_add_to_start_end = 0);
    void unalloc() {
        stbds_arrfree(nodes);
        stbds_arrfree(curvedata);
        root = -1;
    }
} pattern_maker_t;

const char *print_midinote(int note);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);
pattern_t parse_pattern(pattern_maker_t *pm, int index_to_add_to_start_end);
void fill_curve_data_from_string(float *data, const char *s, int n); // responsible for the interpolation of lines
error_msg_t *parse_named_patterns_in_source(const char *s, const char *e, error_msg_t *error_msgs);

const char *skip_path(const char *s, const char *e);
const char *find_end_of_pattern(const char *s, const char *e);
const char *find_start_of_pattern(const char *s, const char *e);

pattern_t *get_pattern(const char *path);

void pretty_print_haps(hap_span_t haps, hap_time from, hap_time to);
