#include "ginkgo.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#define STBDS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"
#include "hash_literal.h"
#include "miniparse.h"

enum NodeType {
#include "node_types.h"
N_LAST
};

static const char *node_type_names[N_LAST] = {
#define NODE(x, ...) #x,
#include "node_types.h"     
};



struct { // global map of all sounds. key is interned name, value likewise allocated once ever. thus we can just store Sound* and/or
         // compare name ptrs.
    char *key;
    Sound *value;
} *g_sounds;


static inline int is_rest(const Sound *sound) {
    if (sound == NULL || sound->name == NULL) // an unspecified name is not a rest.
        return 0;
    return sound->name[1] == 0 && (sound->name[0] == '-' || sound->name[0] == '_' || sound->name[0] == '~');
}

Sound *get_sound(const char *name) { // ...by name.
    Sound *sound = shget(g_sounds, name);
    if (!sound) {
        name = strdup(name); // intern the name!
        sound = (Sound *)calloc(1, sizeof(Sound));
        sound->name = name;
        shput(g_sounds, name, sound);
    }
    return sound;
}


// ansi printing colors
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_RESET "\033[0m"
#define COLOR_BOLD "\033[1m"
#define COLOR_BRIGHT_RED "\033[91m"
#define COLOR_BRIGHT_GREEN "\033[92m"
#define COLOR_BRIGHT_YELLOW "\033[93m"
#define COLOR_BRIGHT_BLUE "\033[94m"
#define COLOR_BRIGHT_MAGENTA "\033[95m"
#define COLOR_BRIGHT_CYAN "\033[96m"
#define COLOR_BRIGHT_WHITE "\033[97m"

const char *print_midinote(int note) {
    static char buf[4];
    if (note < 0 || note >= 128)
        return "";
    const static char notenames[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
    const static char notesharps[12] = {' ', '#', ' ', '#', ' ', ' ', '#', ' ', '#', ' ', '#', ' '};
    buf[0] = notenames[note % 12];
    buf[1] = notesharps[note % 12];
    buf[2] = 0;
    buf[3] = 0;
    buf[(buf[1] == ' ') ? 1 : 2] = note / 12 + '0';
    return buf;
}

void pretty_print(const char *src, Node *nodes, int i, int depth) {
    if (i < 0)
        return;
    int c0 = nodes[i].start;
    int c1 = nodes[i].end;
    printf(COLOR_BLUE "%.*s" COLOR_BRIGHT_YELLOW "%.*s" COLOR_BLUE "%s" COLOR_RESET, c0, src, c1 - c0, src + c0, src + c1);
    for (int j = 0; j < depth + 1; j++) {
        printf("  ");
    }
    printf("%d - %s - maxlen %g val %g %s '%s'\n", i, node_type_names[nodes[i].type], nodes[i].total_length, nodes[i].value.number,
           print_midinote(nodes[i].value.note), nodes[i].value.sound ? nodes[i].value.sound->name : "");
    if (nodes[i].first_child >= 0) {
        pretty_print(src, nodes, nodes[i].first_child, depth + 1);
    }
    if (nodes[i].next_sib >= 0) {
        pretty_print(src, nodes, nodes[i].next_sib, depth);
    }
}

static void error(Parser *p, const char *msg) {
    if (p->errmsg != NULL)
        return;
    p->err = p->i;
    p->errmsg = msg;
    //fprintf(stderr, "ERROR: %s - %.*s" COLOR_BRIGHT_RED "%s" COLOR_RESET "\n", msg, p->err, p->s, p->s + p->err);
}

static void skipws(Parser *p) {
    while (p->i < p->n && isspace(p->s[p->i]))
        p->i++;
}

static int peek(Parser *p) { return (p->i >= p->n) ? -1 : (unsigned char)p->s[p->i]; }
static int peek_i(Parser *p, int i) { return (i >= p->n) ? -1 : (unsigned char)p->s[i]; }

static int consume(Parser *p, int c) {
    if (peek(p) == c) {
        p->i++;
        return 1;
    }
    return 0;
}

static int isclosing(int c) { return c <= 0 || c == ')' || c == ']' || c == '}' || c == '>'; }
static int isopening(int c) { return c == '(' || c == '[' || c == '{' || c == '<'; }
static int isleaf(int c) { return isalnum(c) || c == '_' || c == '-' || c == '.'; }
static int isdelimiter(int c) { return isspace(c) || isclosing(c) || c == ',' || c == '|'; }
static int make_node(Parser *p, int node_type, int first_child, int next_sib, int start, int end) {
    int i = stbds_arrlen(p->nodes);
    Node n = (Node){node_type, start, end, first_child, next_sib, 0};
    stbds_arrput(p->nodes, n);
    return i;
}

static int parse_expr(Parser *p);

static int parse_args(Parser *p, int group_type, int start, int is_poly) {
    int group_node = make_node(p, group_type, -1, -1, start, start);
    int group_link_idx = -1;
    int comma_node = -1;
    int comma_link_idx = -1;
    char comma_char = 0;
    skipws(p);
    int leg_start = p->i;
    while (!isclosing(peek(p))) {
        int i = parse_expr(p);
        if (i == -1)
            return -1;
        if (group_link_idx < 0)
            p->nodes[group_node].first_child = i;
        else
            p->nodes[group_link_idx].next_sib = i;
        group_link_idx = i;
        p->nodes[group_node].end = p->i;
        skipws(p);
        int is_comma_or_pipe = 0;
        if (consume(p, ',')) {
            if (comma_char != 0 && comma_char != ',') {
                error(p, "expected comma in list");
                return -1;
            }
            comma_char = ',';
            is_comma_or_pipe = 1;
        } else if (consume(p, '|')) {
            if (comma_char != 0 && comma_char != '|') {
                error(p, "expected pipe in list");
                return -1;
            }
            comma_char = '|';
            is_comma_or_pipe = 1;
        }
        if (is_comma_or_pipe) {
            int leg_end = p->i - 1;
            skipws(p);
            if (comma_node < 0) {
                comma_node = make_node(p,
                                       (is_poly)           ? N_POLY
                                       : comma_char == ',' ? N_PARALLEL
                                                           : N_RANDOM,
                                       group_node, -1, start, start);
                comma_link_idx = group_node;
            } else {
                p->nodes[comma_link_idx].next_sib = group_node;
                comma_link_idx = group_node;
            }
            group_node = make_node(p, group_type, -1, -1, p->i, p->i);
            group_link_idx = -1;
            leg_start = p->i;
        }
    }
    if (comma_node < 0 && is_poly) {
        // we must wrap in a poly node, even if only 1 leg.
        comma_node = make_node(p, N_POLY, group_node, -1, start, p->i);
        return comma_node;
    }
    if (comma_node >= 0) {
        p->nodes[comma_link_idx].next_sib = group_node;
        p->nodes[comma_node].end = p->i;
        return comma_node;
    }
    return group_node;
}

static int parse_group(Parser *p, char open, char close, int node_type) {
    int start = p->i;
    if (!consume(p, open)) {
        error(p, "expected opening bracket");
        return -1;
    }
    int is_poly = node_type == N_POLY;
    int group_node = parse_args(p, is_poly ? N_CAT : node_type, start, is_poly);
    if (!consume(p, close)) {
        error(p, "expected closing bracket");
        return -1;
    }
    p->nodes[group_node].end = p->i;
    return group_node;
}

int parse_midinote(const char *s, const char *e, int allow_p_prefix) {
    if (allow_p_prefix && e > s + 2 && s[0] == 'P' && s[1] == '_') {
        s += 2;
    }
    if (s >= e)
        return -1;
    if (s[0] < 'A' || s[0] > 'G')
        return -1;
    const static int note_indices[7] = {9, 11, 0, 2, 4, 5, 7};
    int note = note_indices[*s++ - 'A'];
    if (s >= e)
        return -1;
    if (s[0] == 's') {
        note++;
        if (++s >= e)
            return -1;
    } else if (s[0] == 'b') {
        note--;
        if (++s >= e)
            return -1;
    }
    if (s[0] < '0' || s[0] > '9')
        return -1;
    int octave = *s++ - '0';
    if (s != e)
        return -1;
    return note + octave * 12;
}

static int parse_number(const char *s, const char *e, float *number) {
    if (e <= s)
        return 0;
    char *buf = alloca(e - s + 1);
    memcpy(buf, s, e - s);
    buf[e - s] = '\0';
    char *endptr = (char *)e;
    double d = strtod(buf, &endptr);
    if (endptr != buf + (e - s))
        return 0;
    *number = (float)d;
    return 1;
}

static int parse_leaf(Parser *p) {
    int start = p->i;
    while (isleaf(peek(p)))
        p->i++;
    // split the string into parts separated by _; valid parts can be a number, a note, or a sound.
    int node = make_node(p, N_LEAF, -1, -1, start, p->i);
    Value *v = &p->nodes[node].value;
    for (int partstart = start; partstart < p->i; partstart++) {
        int partend = partstart;
        for (; partend < p->i && p->s[partend] != '_'; partend++)
            ;
        int note = parse_midinote(p->s + partstart, p->s + partend, 0);
        float number = 0.f;
        if (note >= 0) {
            v->note = note;
        } else if (parse_number(p->s + partstart, p->s + partend, &number)) {
            v->number = number;
        } else {
            char *name = alloca(partend - partstart + 1);
            memcpy(name, p->s + partstart, partend - partstart);
            name[partend - partstart] = '\0';
            v->sound = get_sound(name);
        }
        partstart = partend + 1;
    }
    return node;
}

static int parse_expr_inner(Parser *p) {
    skipws(p);
    switch (peek(p)) {
    case '[':
        return parse_group(p, '[', ']', N_FASTCAT);
    case '<':
        return parse_group(p, '<', '>', N_CAT);
    case '{':
        return parse_group(p, '{', '}', N_POLY);
    default:
        if (isleaf(peek(p)))
            return parse_leaf(p);
    }
    error(p, "unexpected token");
    return -1;
}

static int parse_euclid(Parser *p, int node) {
    int euclid_node0 = parse_expr(p);
    if (euclid_node0 < 0)
        return -1;
    skipws(p);
    if (!consume(p, ',')) {
        error(p, "expected comma in euclid");
        return -1;
    }
    int euclid_node1 = parse_expr(p);
    if (euclid_node1 < 0)
        return -1;
    int euclid_node2 = -1;
    skipws(p);
    if (consume(p, ',')) {
        euclid_node2 = parse_expr(p);
    }
    if (!consume(p, ')')) {
        error(p, "expected closing bracket in euclid");
        return -1;
    }
    int euclid_node = make_node(p, N_OP_EUCLID, node, -1, p->nodes[node].start, p->i);
    p->nodes[node].next_sib = euclid_node0;
    p->nodes[euclid_node0].next_sib = euclid_node1;
    p->nodes[euclid_node1].next_sib = euclid_node2;
    return euclid_node;
}

static int parse_op(Parser *p, int left_node, int node_type, int num_params, int optional_right) {
    if (left_node < 0)
        return -1;
    skipws(p);
    if (node_type == N_OP_EUCLID) {
        return parse_euclid(p, left_node); // euclid node is different - it has a closing ) and comma separated args. because, idk. history.
    }
    int right_node;
    int ch = peek(p);
    if (optional_right && !isleaf(ch) && !isopening(ch)) {
        right_node = -1;
    } else {
        right_node = parse_expr_inner(p); // we parse inner here to avoid consuming more ops on the RHS; instead we will do it in
                                          // the while loop inside parse_expr.
        p->nodes[left_node].next_sib = right_node;
    }
    int parent_node = make_node(p, node_type, left_node, -1, p->nodes[left_node].start, p->i);
    return parent_node;
}


static int parse_expr(Parser *p) {
    int node = parse_expr_inner(p);
    skipws(p);
    if (node < 0)
        return -1;
    while (!isdelimiter(peek(p))) {
        int old_pos = p->i;
        if (peek(p) == '_') { // shorthand for elongate. we only allow it to be a single character
            if (isdelimiter(peek_i(p, p->i + 1))) {
                // ok count the _ tokens!
                int count = 1;
                while (peek(p) == '_' && isdelimiter(peek_i(p, p->i + 1))) {
                    consume(p, '_');
                    skipws(p);
                    count++;
                }
                assert(count >= 2);
                int elongate_node = make_node(p, N_OP_ELONGATE, node, -1, p->nodes[node].start, p->i);
                p->nodes[elongate_node].value.number = count;
                node = elongate_node;
                break;
            }
        }
        else if (peek(p) == '%') {
            if (p->nodes[node].type == N_POLY) {
                consume(p, '%');
                int modulo_node = parse_leaf(p);
                if (modulo_node < 0 || p->nodes[modulo_node].value.number <= 0.f) {
                    error(p, "expected positive number in modulo");
                    return -1;
                }
                // we store the modulo constant in our value.number. a bit of a hack...
                p->nodes[node].value.number = p->nodes[modulo_node].value.number;
            }
        } else if (peek(p) == '(') {
            consume(p,'(');
            node = parse_euclid(p, node);
        } else {
            int i = p->i;
            while (!isdelimiter(p->s[i]) && !isopening(p->s[i]) && !isdigit(p->s[i])) ++i;
            if (i > p->i) {
                uint32_t name_hash = literal_hash_span(p->s + p->i, p->s + i);
                switch (name_hash) {
                    #define NODE(x, ...) 
                    #define OP(op_type, srcname, numparams, optional_right) case HASH(srcname): p->i = i; node = parse_op(p, node, op_type, numparams, optional_right); break;
                    #include "node_types.h"
                }
            }
        }
        if (p->i == old_pos)
            break; // no progress. stop here.
        skipws(p);
    }
    return node;
}

/*
everything report length 1 except replicate and elongate that report their right child's max value.
fastcat needs to know sum of children's lengths to compute its fast ratio.
poly uses its first child's children's sum as its speedup, else its value
*/
static float get_length(Parser *p, int node) {
    if (node < 0)
        return 1.f;
    Node *n = &p->nodes[node];
    if (n->type == N_OP_REPLICATE || n->type == N_OP_ELONGATE) {
        return n->value.number;
    }
    return 1.f;
}

static void update_lengths(Parser *p, int node) {
    if (node < 0)
        return;
    Node *n = &p->nodes[node];
    float max_value = 0.f;
    float total_length = 0.f;
    for (int i = n->first_child; i >= 0; i = p->nodes[i].next_sib) {
        update_lengths(p, i);
        max_value = maxf(max_value, p->nodes[i].value.number);
        total_length += get_length(p, i);
    }
    n->total_length = total_length;
    if (n->type != N_POLY && n->type != N_LEAF) {
        n->value.number = max_value;
    }
}

static inline void squeeze(Parser *p, int *parent) {
    // remove all 
    //  CAT nodes with 1 child;
    //  FASTCAT nodes with 1 child and a total child length of 1;
    //  PARALLEL nodes with 1 child;
    int *prevlink = parent;
    while (*prevlink >= 0) { // loop over siblings
        Node *n = p->nodes + *prevlink;
        squeeze(p, &n->first_child);
        bool has_one_child = n->first_child >= 0 && p->nodes[n->first_child].next_sib < 0;
        if (has_one_child && (n->type == N_CAT || (n->type == N_FASTCAT && n->total_length == 1.f) || n->type == N_PARALLEL)) {
            // we want to replace this node with its only child
            int old_sibling = n->next_sib;
            *prevlink = n->first_child;
            n = p->nodes + *prevlink;
            n->next_sib = old_sibling;
        }
        prevlink = &n->next_sib;
    }
}

int parse_pattern(Parser *p) {
    p->i = 0;
    if (p->nodes) stbds_arrsetlen(p->nodes, 0);
    p->root = -1;
    p->err = 0;
    p->errmsg = NULL;
    p->root = parse_args(p, N_FASTCAT, 0, 0);
    update_lengths(p, p->root);
    squeeze(p, &p->root);
    return p->err;
}

static inline void print_haps(Parser *p, Hap *haps, int n) {
    for (int i = 0; i < n; i++) {
        Hap h = haps[i];
        Node *n = p->nodes + h.node;
        printf("%f %f - node %d \"%.*s\":%d v=%g\n", h.t0, h.t1, h.node, n->end - n->start, p->s + n->start, h.sound_idx,
               n->value.number);
    }
}

#define FLAG_NONE 0
#define FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES 1
#define FLAG_INCLUSIVE 2 // if set, we include haps that overlap on the left side.


static inline uint32_t hash2_pcg(uint32_t a, uint32_t b) {
    const uint64_t MUL = 6364136223846793005ull;
    uint64_t state = ((uint64_t)a << 32) | b;    // pack inputs
    uint64_t inc   = ((uint64_t)b << 1) | 1ull;  // odd stream
    
    state = state * MUL + inc;                   // LCG step
    
    uint32_t xorshifted = (uint32_t)(((state >> 18) ^ state) >> 27);
    uint32_t rot = (uint32_t)(state >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static inline uint32_t hasht_int(uint32_t seed, float time) {
    return hash2_pcg(seed, *(uint32_t*)&time);
}

static inline float hasht_float(uint32_t seed, float time) {
    return (hasht_int(seed, time)&0xffffff) * (1.f/16777216.f);
}

static inline int cmp_haps_by_t0(const Hap *a, const Hap *b) {
    return a->t0 < b->t0 ? -1 : a->t0 > b->t0 ? 1 : 0;
}

static inline void sort_haps_by_t0(Hap *haps, int count) {
    qsort(haps, count, sizeof(Hap), (int(*)(const void*,const void*))cmp_haps_by_t0);
}

static Hap *make_haps(Parser *p, int nodeidx, float t0, float t1, float tscale, float tofs, Hap **haps, int flags, int sound_idx,
                      float note_prob) {
    if (nodeidx < 0 || t0 >= t1)
        return *haps;
    uint32_t seed = hash2_pcg(p->rand_seed, nodeidx);
    Node *n = p->nodes + nodeidx;
    if (n->type == N_LEAF && (flags & FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES)) {
        Hap h = (Hap){t0 * tscale + tofs, t1 * tscale + tofs, nodeidx, sound_idx};
        stbds_arrput(*haps, h);
        return *haps;
    }
    switch (n->type) {
    case N_OP_EUCLID: {
        int child = n->first_child;
        if (child <0) return *haps;
        int setsteps_nodeidx = p->nodes[child].next_sib;
        if (setsteps_nodeidx <0) return *haps;
        int numsteps_nodeidx = p->nodes[setsteps_nodeidx].next_sib;
        if (numsteps_nodeidx <0) return *haps;
        int rot_nodeidx = p->nodes[numsteps_nodeidx].next_sib;
        Hap *setsteps_haps = NULL, *numsteps_haps = NULL, *rot_haps = NULL;
        make_haps(p, setsteps_nodeidx, t0, t1, 1.f, 0.f, &setsteps_haps, FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES | FLAG_INCLUSIVE, 0, 1.f);
        make_haps(p, numsteps_nodeidx, t0, t1, 1.f, 0.f, &numsteps_haps, FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES | FLAG_INCLUSIVE, 0, 1.f);
        make_haps(p, rot_nodeidx, t0, t1, 1.f, 0.f, &rot_haps, FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES | FLAG_INCLUSIVE, 0, 1.f);
        int setsteps_hap_count = stbds_arrlen(setsteps_haps);
        int numsteps_hap_count = stbds_arrlen(numsteps_haps);
        int rot_hap_count = stbds_arrlen(rot_haps);
        sort_haps_by_t0(setsteps_haps, setsteps_hap_count);
        sort_haps_by_t0(numsteps_haps, numsteps_hap_count);
        sort_haps_by_t0(rot_haps, rot_hap_count);
        int setsteps_hapidx = 0, numsteps_hapidx = 0, rot_hapidx = 0;
        float from = t0;
        while (from < t1) {
            float to = t1;
            to = minf(to, setsteps_hapidx+1 < setsteps_hap_count ? setsteps_haps[setsteps_hapidx+1].t0 : t1);
            to = minf(to, numsteps_hapidx+1 < numsteps_hap_count ? numsteps_haps[numsteps_hapidx+1].t0 : t1);
            to = minf(to, rot_hapidx+1 < rot_hap_count ? rot_haps[rot_hapidx+1].t0 : t1);
            if (to <= from) break;
            int setsteps = setsteps_hapidx < setsteps_hap_count ? p->nodes[setsteps_haps[setsteps_hapidx].node].value.number : 0;
            int numsteps = numsteps_hapidx < numsteps_hap_count ? p->nodes[numsteps_haps[numsteps_hapidx].node].value.number : 0;
            int rot = rot_hapidx < rot_hap_count ? p->nodes[rot_haps[rot_hapidx].node].value.number : 0;
            float speed_scale = numsteps;
            float child_t0 = from * speed_scale;
            float child_t1 = to * speed_scale;
            for (int stepidx = (int)child_t0; stepidx < (int)child_t1; stepidx++) {
                if (stepidx>=child_t0 && stepidx<child_t1 && euclid_rhythm(stepidx, setsteps, numsteps, rot)) {                    
                    make_haps(p, child, stepidx, stepidx+1, tscale / speed_scale, tofs, haps, flags, sound_idx, note_prob);
                }
            }
            from = to;
            while (setsteps_hapidx+1 < setsteps_hap_count && setsteps_haps[setsteps_hapidx+1].t0 <= from) 
                setsteps_hapidx++;
            while (numsteps_hapidx+1 < numsteps_hap_count && numsteps_haps[numsteps_hapidx+1].t0 <= from)
                numsteps_hapidx++;
            while (rot_hapidx+1 < rot_hap_count && rot_haps[rot_hapidx+1].t0 <= from)
                rot_hapidx++;
        }
        stbds_arrfree(setsteps_haps);
        stbds_arrfree(numsteps_haps);
        stbds_arrfree(rot_haps);
        break; }
    case N_OP_REPLICATE:
    case N_OP_ELONGATE:
    case N_OP_DEGRADE:
    case N_OP_IDX:
    case N_OP_DIVIDE:
    case N_OP_TIMES: {
        if (n->first_child < 0)
            return *haps;
        Node *nleft = p->nodes + n->first_child;
        if (nleft->next_sib >= 0) {
            // create new haps on the right side. then iterate over them, one at a time, and query haps for the left scaled
            // appropriately.
            Hap *right_haps = NULL;
            make_haps(p, nleft->next_sib, t0, t1, 1.f, 0.f, &right_haps, FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES | FLAG_INCLUSIVE,
                      0, 1.f);
            // printf("queried right haps from %g to %g, got %d haps back\n", t0, t1, (int)stbds_arrlen(right_haps));
            // print_haps(p, right_haps, stbds_arrlen(right_haps));
            for (int i = 0; i < stbds_arrlen(right_haps); i++) {
                Hap *right_hap = right_haps + i;
                float speed_scale = 1.f;
                float degrade_amount = 0.f;
                float right_value = p->nodes[right_hap->node].value.number;
                if (n->type == N_OP_TIMES)
                    speed_scale = right_value;
                else if (n->type == N_OP_DIVIDE || n->type == N_OP_ELONGATE)
                    speed_scale = 1.f / (right_value ? right_value : 1.f);
                else if (n->type == N_OP_IDX)
                    sound_idx = (int)right_value;
                else if (n->type == N_OP_DEGRADE)
                    degrade_amount = right_value;
                if (speed_scale <= 0.f)
                    speed_scale = 1.f;
                float child_t0 = right_hap->t0 * speed_scale; //  maxf(t0, right_hap->t0) * speed_scale;
                float child_t1 = right_hap->t1 * speed_scale; // minf(t1, right_hap->t1) * speed_scale;
                make_haps(p, n->first_child, child_t0, child_t1,
                          tscale / speed_scale, tofs, haps, flags, sound_idx, note_prob * (1.f - degrade_amount));
            }
            stbds_arrfree(right_haps);
        } else {
            // no value? proxy to the left. for degrade, if there is no right value, we assume 50% default.
            make_haps(p, n->first_child, t0, t1, tscale, tofs, haps, flags, sound_idx,
                      n->type == N_OP_DEGRADE ? note_prob * 0.5f : note_prob);
        }
        return *haps;
        break;
    }
    case N_RANDOM: {
        int num_children=0;
        for (int i=n->first_child; i>=0; i=p->nodes[i].next_sib) ++num_children;
        if (!num_children)
            return *haps;
        int *kids=(int*)alloca(num_children*4);
        num_children=0;
        for (int i=n->first_child; i>=0; i=p->nodes[i].next_sib) kids[num_children++]=i;
        for (int t=(int)t0; t<t1; ++t) {
            float from = t, to=t+1.f;
            int kid = kids[hasht_int(seed,from)%num_children];
            int inclusive_left = (flags & FLAG_INCLUSIVE) && to > t0;
            if ((from >= t0 || inclusive_left) && from < t1) {
                make_haps(p, kid, from, to, tscale, tofs, haps, flags, sound_idx, note_prob);
            }
        }
        break;
    }
    case N_FASTCAT:
        t0 *= n->total_length;
        t1 *= n->total_length;
        tscale /= n->total_length;
    case N_LEAF:
    case N_CAT: {
        int loop_index = (int)floorf(t0 / n->total_length);
        int child = n->first_child;
        float from = loop_index * n->total_length;
        while (from < t1) {
            if (child >= 0) {
                Node *child_n = p->nodes + child;
                float child_length = get_length(p, child);
                if (child_length <= 0.f)
                    child_length = 1.f;
                float to = from + child_length;
                int inclusive_left = (flags & FLAG_INCLUSIVE) && to > t0;
                if ((from >= t0 || inclusive_left) && from < t1) {
                    // this child overlaps the query range
                    float child_from = loop_index * child_length;
                    float child_to = child_from + child_length;
                    make_haps(p, child, child_from, child_to, tscale, tofs + (from - child_from) * tscale, haps, flags,
                              sound_idx, note_prob);
                }
                from = to;
                child = child_n->next_sib;
                if (child < 0) {
                    child = n->first_child;
                    loop_index++;
                }
            } else { // leaf
                float to = from + 1.f;
                int inclusive_left = (flags & FLAG_INCLUSIVE) && to > t0;
                if ((from >= t0 || inclusive_left) && from < t1) {
                    float output_t0 = from * tscale + tofs;
                    if (note_prob >= 1.f || hasht_float(seed, output_t0) <= note_prob) {
                        if (nodeidx < 0 || !is_rest(p->nodes[nodeidx].value.sound)) {
                            Hap h = (Hap){output_t0, to * tscale + tofs, nodeidx, sound_idx};
                            stbds_arrput(*haps, h);
                        }
                    }
                }
                from = to;
                loop_index++;
            }
        }
        break;
    }
    case N_POLY:
    case N_PARALLEL: {
        int child = n->first_child;
        float speed_scale = 1.f;
        if (n->type == N_POLY) {
            speed_scale = (n->value.number > 0) ? n->value.number : (child >= 0) ? p->nodes[child].total_length : 1.f;
        }
        while (child >= 0) {
            make_haps(p, child, t0 * speed_scale, t1 * speed_scale, tscale / speed_scale, tofs, haps, flags, sound_idx,
                      note_prob);
            child = p->nodes[child].next_sib;
        }
        break;
    }
    }
    return *haps;
}

char* print_pattern_chart(Parser *p) { // returns stb_ds string
    Hap *haps = NULL;
    make_haps(p, p->root, 0.0f, 4.0f, 1.0f, 0.f, &haps, FLAG_NONE, 0, 1.f);
    const char *header =
        "      time   0   |   |   |   v   |   |   |   1   |   |   |   v   |   |   |   2   |   |   |   v   |   |   |   3   |   |   "
        "|   v   |   |   |   4\n";
    char *result = NULL;
    memmove(stbds_arraddnptr(result, strlen(header)), header, strlen(header));
    for (int i = 0; i < stbds_arrlen(haps); i++) {
        Node *n = &p->nodes[haps[i].node];
        int j;
        for (j = 0; j < i; ++j) {
            Node *nj = &p->nodes[haps[j].node];
            if (n->value.sound == nj->value.sound && haps[j].sound_idx == haps[i].sound_idx)
                break;
        }
        if (j < i)
            continue;
        char line[130];
        int left = snprintf(line, 128, "%10s:%d ", n->value.sound ? n->value.sound->name : "", haps[i].sound_idx);
        memcpy(stbds_arraddnptr(result, left), line, left);
        for (int j = 0; j < 128; ++j)
            line[j] = '.';
        line[128]='\n';
        for (int j = i; j < stbds_arrlen(haps); ++j) {
            Node *nj = &p->nodes[haps[j].node];
            if (nj->value.sound != n->value.sound || haps[j].sound_idx != haps[i].sound_idx)
                continue;
            int x1 = (int)(haps[j].t0 * 32.f);
            int x2 = (int)(haps[j].t1 * 32.f);
            if (x1 >= 0 && x1 < 128) {
                if (line[x1] == '.')
                    line[x1] = '*';
                else if (line[x1] >= '1' && line[x1] < '9')
                    line[x1]++;
                else if (line[x1] == '9')
                    line[x1] = '!';
                else
                    line[x1] = '2';
            }
            for (int x = x1 + 1; x < x2; ++x) {
                if (x >= 0 && x < 128)
                    line[x] = (x == x2 - 1) ? '>' : '=';
            }
        }
        memcpy(stbds_arraddnptr(result, 129), line, 129);
    }
    stbds_arrput(result, 0);
    stbds_arrfree(haps);
    return result;
}

void test_minipat(void) {
    // const char *s = "sd,<oh hh>,[[bd bd:1 -] rim]";
    //const char *s = "{bd sd, rim hh oh}%4";
    //const char *s = "[bd | sd | rim]*8";
    //const char *s = "[[sd] [bd]]"; // test squeeze
    //const char *s = "[sd*<2 1> bd(<3 1 4>,8)]"; // test euclid
    const char *s = "[hi lo/2]"; // test divide
    //const char *s = "<bd sd>";
    // const char *s = "{c eb g, c2 g2}%4";
    // const char *s = "[bd <hh oh>,rim*<4 8>]";
    // const char *s = "[bd,sd*1.1]";
    printf("\nparsing " COLOR_BRIGHT_GREEN "\"%s\"\n" COLOR_RESET "\n", s);
    Parser p={s,strlen(s)};
    parse_pattern(&p);
    pretty_print(s, p.nodes, p.root, 0);
    char *chart = print_pattern_chart(&p);
    printf("%s\n", chart);
    stbds_arrfree(chart);
}
