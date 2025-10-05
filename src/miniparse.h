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

enum EValueType : uint8_t { // for Node.value_type
    VT_NONE,
    VT_NUMBER,
    VT_NOTE,
    VT_SOUND,
};

typedef struct Node {
    uint8_t type;
    uint8_t value_type;  // note, number, sound
    int32_t start, end;  // half-open [start,end) character range (for syntax hilighting)
    int32_t first_child; // index or -1
    int32_t next_sib;    // index or -1 - linked list of siblings
    float total_length;  // total length of all children (nb elongated nodes are longer than 1)
    float min_value, max_value; // parsed value of the node
} Node;



typedef struct Hap { 
    float t0, t1; 
    int node; // index of the node that generated this hap.
    uint32_t valid_params; // which params have been assigned for this hap.
    float params[P_LAST];
} Hap;

typedef struct Pattern { // a parsed version of a mini notation string
    const char *s; // ...the string.
    int32_t n;   // length of string
    int32_t i;   // current position in string during parsing.
    Node *nodes; // stb_ds
    float *curvedata; // stb_ds
    int root;    // index of root node
    int err;     // 0 ok, else position of first error
    const char *errmsg;
    uint32_t rand_seed;
} Pattern;

const char *print_midinote(int note);
int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);
int parse_pattern(Pattern *p);
char* print_pattern_chart(Pattern *p);
Hap *make_haps(Pattern *p, int nodeidx, float t0, float t1, float tscale, float tofs, Hap **haps, int flags);
void fill_curve_data_from_string(float *data, const char *s, int n); // responsible for the interpolation of lines

// base64 with # being 64 :) so we can do full range 0-64
extern const char btoa_tab[65];
extern const uint8_t atob_tab[256];
