#pragma once

// init only ones are allowed to mutate the sound map.
Sound *get_sound_init_only(const char *name); // ...by name.
Sound *add_alias_init_only(const char *short_alias, const char *long_name);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix); 

enum {
    #define X(x, ...) x,
    #include "params.h"
    P_LAST
};

enum EValueType : int8_t { // for Node.value_type
    VT_NONE = -1,
    VT_NUMBER = P_NUMBER,
    VT_NOTE = P_NOTE,
    VT_SOUND = P_SOUND,
};

typedef struct fraction_t { // helps with rounding a bit... didn't go full rational yet :)
    float num, denom;
    fraction_t operator*(float f) { return (f>=1) ? fraction_t{num*f, denom} : fraction_t{num, denom/f}; }
    fraction_t operator/(float f) { return (f>=1) ? fraction_t{num, denom*f} : fraction_t{num/f, denom}; }
    fraction_t inverse() const { return {denom, num}; }
} fraction_t;


typedef struct Node {
    uint8_t type;
    int8_t value_type;  // note, number, sound
    int32_t start, end;  // half-open [start,end) character range (for syntax hilighting)
    int32_t first_child; // index or -1
    int32_t next_sib;    // index or -1 - linked list of siblings
    int num_children;
    float total_length;  // total length of all children (nb elongated nodes are longer than 1)
    float min_value, max_value; // parsed value of the node
} Node;

//#define INTEGER_HAP_TIME 
#ifdef INTEGER_HAP_TIME
typedef int hap_time; // 16 bit fixed point :)
static const int cycle_shift = 16;
static const hap_time hap_cycle_time = 1<<cycle_shift;
static const hap_time epsilon = 2;
static inline hap_time scale_time(hap_time t, fraction_t f) {
    return (int64_t(t) * int64_t(f.num*65536.f)) / (f.denom*65536.f);
}
static inline hap_time floor2cycle(hap_time t) {
    return t & ~(hap_cycle_time - 1);
}
static inline int haptime2cycleidx(hap_time t) {
    return t >> cycle_shift;
}
static inline hap_time sample_idx2hap_time(int sample_idx, float bpm) {
    return (sample_idx*double(bpm*hap_cycle_time/(SAMPLE_RATE*240.f)));
}
#else
typedef float hap_time;
static const hap_time hap_cycle_time = 1.f;
static const hap_time epsilon = 2.f/SAMPLE_RATE;
static inline hap_time scale_time(hap_time t, fraction_t f) {
    return (t * f.num) / f.denom;
}
static inline hap_time floor2cycle(hap_time t) {
    return floor(t);
}
static inline int haptime2cycleidx(hap_time t) {
    return (int)t;
}
static inline hap_time sample_idx2hap_time(int sample_idx, float bpm) {
    return sample_idx*(bpm/(SAMPLE_RATE*240.f));
}
#endif 

typedef struct hap_t { 
    hap_time t0, t1; 
    int node; // index of the node that generated this hap.
    uint32_t valid_params; // which params have been assigned for this hap.
    int hapid;
    float params[P_LAST];
} hap_t;

typedef struct hap_span_t {
    hap_t *s, *e;
    inline bool empty() const { return s>=e; }
    inline bool hasatleast(int i) const { return s+i<e; }
} hap_span_t;

#define FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES 1

typedef struct bfs_node_t {
    uint8_t type;
    int8_t value_type;  // note, number, sound
    uint16_t num_children;
    int32_t first_child; // index or -1
} bfs_node_t;


typedef struct pattern_t { // a parsed version of a min notation string
    const char *key;
    Node *nodes; // stb_ds // TODO remove
    float *curvedata; // stb_ds
    int root;    // index of root node // TODO remove

    // bfs 
    int_pair_t *bfs_start_end; // source code ranges
    float_pair_t *bfs_min_max_value; // parsed value of the node
    float *bfs_nodes_total_length;
    bfs_node_t *bfs_nodes;

    float get_length(int nodeidx);
    hap_span_t _make_haps(hap_span_t &dst, hap_span_t tmp, int nodeidx, hap_time t0, hap_time t1, int hapid);
    void _append_hap(hap_span_t &dst, int nodeidx, hap_time t0, hap_time t1, int hapid);
    hap_span_t make_haps(hap_span_t dst, hap_span_t tmp, hap_time t0, hap_time t1) { 
        return _make_haps(dst, tmp, root, t0, t1, 1);
    }
    void unalloc() {
        stbds_arrfree(nodes);
        stbds_arrfree(curvedata);
        stbds_arrfree(bfs_start_end);
        stbds_arrfree(bfs_min_max_value);
        stbds_arrfree(bfs_nodes);
        root=-1;
    }
} pattern_t;


typedef struct pattern_maker_t {
    Node *nodes; // stb_ds
    float *curvedata; // stb_ds
    const char *s; // ...the source string.
    int root;    // index of root node
    int32_t n;   // length of string
    int32_t i;   // current position in string during parsing.
    int err;     // 0 ok, else position of first error
    const char *errmsg;
    pattern_t get_pattern(const char *key=NULL);
    void unalloc() {
        stbds_arrfree(nodes);
        stbds_arrfree(curvedata);
        root=-1;
    }
} pattern_maker_t;

const char *print_midinote(int note);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);
pattern_t parse_pattern(pattern_maker_t *pm);
void fill_curve_data_from_string(float *data, const char *s, int n); // responsible for the interpolation of lines
void parse_named_patterns_in_c_source(const char *s, const char *e);

const char *skip_path(const char *s, const char *e);
const char *find_end_of_pattern(const char *s, const char *e);

void pretty_print_haps(hap_span_t haps, hap_time from, hap_time to);

// base64 with # being 64 :) so we can do full range 0-64
extern const char btoa_tab[65];
extern const uint8_t atob_tab[256];
