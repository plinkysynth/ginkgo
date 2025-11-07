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
    int8_t value_type;  // note, number, sound
    int32_t start, end;  // half-open [start,end) character range (for syntax hilighting)
    int linenumber; // whate line we are on
    int32_t first_child; // index or -1
    int32_t next_sib;    // index or -1 - linked list of siblings
    int num_children;
    float total_length;  // total length of all children (nb elongated nodes are longer than 1)
    float min_value, max_value; // parsed value of the node
} Node;


void merge_hap(hap_t *dst, hap_t *src); // copy valid fields from src onto dst

typedef struct hap_span_t {
    hap_t *s, *e;
    inline bool empty() const { return s>=e; }
    inline bool hasatleast(int i) const { return s+i<e; }
} hap_span_t;


typedef struct bfs_node_t {
    uint8_t type;
    int8_t value_type;  // note, number, sound
    uint16_t num_children;
    int32_t first_child; // index or -1
} bfs_node_t;

typedef int (*filter_cb_t)(hap_t *left_hap, hap_t *right_hap, int new_hapid, hap_time when); // returns how many copies to make (0=filter...)
typedef int (*value_cb_t)(hap_t *target, hap_t **right_hap, size_t context, hap_time when); // return 1 if we want to keep the hap.

typedef struct float_minmax_t {
    float mn, mx;
} float_minmax_t;

typedef struct token_info_t {
    int start, end;
    float last_evaled_glfw_time;
    float local_time_of_eval;
} token_info_t;

typedef struct pattern_t { // a parsed version of a min notation string
    const char *key;
    float *curvedata; // stb_ds

    // bfs 
    token_info_t *bfs_start_end; // source code ranges
    float_minmax_t *bfs_min_max_value; // parsed value of the node
    float *bfs_grid_time_offset;
    float *bfs_kids_total_length;
    bfs_node_t *bfs_nodes;

    float get_length(int nodeidx);
    void _filter_haps(hap_span_t left_haps, hap_time speed_scale, hap_time tofs, hap_time when);
    int _apply_values(hap_span_t &dst, int tmp_size, float viz_time, hap_t *structure_hap, int value_node_idx,filter_cb_t filter_cb, value_cb_t value_cb, size_t context, hap_time when, int num_rhs=1);
    void _apply_unary_op(hap_span_t &dst, int tmp_size, float viz_time, int first_child, hap_time when, int hapid, filter_cb_t filter_cb, value_cb_t value_cb, size_t context, int num_rhs=1);
    hap_span_t _make_haps(hap_span_t &dst, int tmp_size, float viz_time, int nodeidx, hap_time when, int hapid);
    bool _append_leaf_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid); // does random range 
    bool _append_number_hap(hap_span_t &dst, int nodeidx, int hapid, float value); 
    hap_span_t make_haps(hap_span_t dst, int tmp_size, float viz_time, hap_time when) { 
        return _make_haps(dst, tmp_size, viz_time, 0, when, 1);
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


typedef struct pattern_maker_t {
    Node *nodes; // stb_ds
    float *curvedata; // stb_ds
    const char *s; // ...the source string.
    int root;    // index of root node
    int32_t n;   // length of string
    int32_t i;   // current position in string during parsing.
    int linecount; // counts \n
    int err;     // 0 ok, else position of first error
    const char *errmsg;
    pattern_t make_pattern(const char *key=NULL, int index_to_add_to_start_end=0);
    void unalloc() {
        stbds_arrfree(nodes);
        stbds_arrfree(curvedata);
        root=-1;
    }
} pattern_maker_t;

const char *print_midinote(int note);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);
pattern_t parse_pattern(pattern_maker_t *pm, int index_to_add_to_start_end);
void fill_curve_data_from_string(float *data, const char *s, int n); // responsible for the interpolation of lines
void parse_named_patterns_in_source(const char *s, const char *e);

const char *skip_path(const char *s, const char *e);
const char *find_end_of_pattern(const char *s, const char *e);
const char *find_start_of_pattern(const char *s, const char *e);

pattern_t *get_pattern(const char *path);

void pretty_print_haps(hap_span_t haps, hap_time from, hap_time to);

// base64 with # being 64 :) so we can do full range 0-64
extern const char btoa_tab[65];
extern const uint8_t atob_tab[256];
