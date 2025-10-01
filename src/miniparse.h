#pragma once

/* TODO - synth stuff etc :)
typedef struct Wave {
    const char *filename;
    short *wavedata;
    int length;
    int sample_rate;
    int note;
} Wave;
*/
typedef struct Sound {
    const char *name; // interned name.
    // Wave *waves;      // stb_ds array, indexed by idx or searched by nearest note
} Sound;

Sound *get_sound(const char *name); // ...by name.

typedef struct Value { // the value associated with a node in the parse tree. nb sound idx like bd:3 is assigned later at hap-time.
    Sound *sound;
    union {
        struct {
            // if node type is not N_CURVE
            float number;
            float note;
        };
        struct {
            // if node type is N_CURVE
            float first_curve_data_idx; // we keep this as float so its the same type as number and note in case of bugs...
            float num_curve_data;
        };
    };
} Value;

typedef struct Node {
    int32_t type;
    int32_t start, end;  // half-open [start,end) character range (for syntax hilighting)
    int32_t first_child; // index or -1
    int32_t next_sib;    // index or -1 - linked list of siblings
    float total_length;  // total length of all children (nb elongated nodes are longer than 1)
    Value value;         // for a leaf, the value; for others, maximum number value over all children.
} Node;

typedef struct Hap {
    float t0, t1;
    int node;
    int sound_idx; // as in, bd:3. 
    float local_t0, local_t1; // the local time range of the hap, in the pattern's time range.
} Hap;

typedef struct Pattern {
    const char *s;
    int32_t n;   // length of string
    int32_t i;   // current position in string
    Node *nodes; // stb_ds
    float *curvedata; // stb_ds
    int root;    // index of root node
    int err;     // 0 ok, else position of first error
    const char *errmsg;
    uint32_t rand_seed;
} Pattern;

const char *print_midinote(int note);
int parse_midinote(const char *s, const char *e, int allow_p_prefix);
int parse_pattern(Pattern *p);
char* print_pattern_chart(Pattern *p);
Hap *make_haps(Pattern *p, int nodeidx, float t0, float t1, float tscale, float tofs, Hap **haps, int flags, int sound_idx, float note_prob);
void fill_curve_data_from_string(float *data, const char *s, int n); // responsible for the interpolation of lines

#define FLAG_NONE 0 // for make_haps

// base64 with # being 64 :) so we can do full range 0-64
extern const char btoa_tab[65];
extern const uint8_t atob_tab[256];
