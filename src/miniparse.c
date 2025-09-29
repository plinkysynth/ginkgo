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

enum NodeType {
    N_LEAF,
    N_CAT,
    N_FASTCAT,
    N_PARALLEL,
    N_IDX,
    N_TIMES,
    N_DIVIDE,
    N_DEGRADE,
    N_REPLICATE,
    N_ELONGATE,
    N_EUCLID,
    N_RANDOM,
    N_POLY,
    N_LAST
};
static const char *node_type_names[N_LAST] = {"LEAF",    "CAT",       "FASTCAT",  "PARALLEL", "IDX",    "TIMES", "DIVIDE",
                                              "DEGRADE", "REPLICATE", "ELONGATE", "EUCLID",   "RANDOM", "POLY"};

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

struct { // global map of all sounds. key is interned name, value likewise allocated once ever. thus we can just store Sound* and/or
         // compare name ptrs.
    char *key;
    Sound *value;
} *g_sounds;

static inline int is_rest(const char *name) {
    if (name == NULL) // an unspecified name is not a rest.
        return 0;
    return name[1] == 0 && (name[0] == '-' || name[0] == '_' || name[0] == '~');
}

static inline Sound *get_sound(const char *name) { // ...by name.
    Sound *sound = shget(g_sounds, name);
    if (!sound) {
        name = strdup(name); // intern the name!
        sound = (Sound *)calloc(1, sizeof(Sound));
        sound->name = name;
        shput(g_sounds, name, sound);
    }
    return sound;
}

typedef struct Value { // the value associated with a node in the parse tree. nb sound idx like bd:3 is assigned later at hap-time.
    Sound *sound;
    float number;
    float note;
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
    int sound_idx; // as in, bd:3
} Hap;

typedef struct Parser {
    const char *s;
    int32_t n;   // length of string
    int32_t i;   // current position in string
    Node *nodes; // stb_ds
    int root;    // index of root node
    int err;     // 0 ok, else position of first error
    const char *errmsg;
} Parser;

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
    char notenames[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
    char notesharps[12] = {' ', '#', ' ', '#', ' ', ' ', '#', ' ', '#', ' ', '#', ' '};
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
    fprintf(stderr, "ERROR: %s - %.*s" COLOR_BRIGHT_RED "%s" COLOR_RESET "\n", msg, p->err, p->s, p->s + p->err);
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
static int isleaf(int c) { return isalnum(c) || c == '_' || c == '-' || c == '.'; }
static int isdelimiter(int c) { return isspace(c) || isclosing(c) || c == ',' || c == '|'; }
static int isop(int c) { return c == '*' || c == ':' || c == '/' || c == '?' || c == '!' || c == '@' || c == '%' || c == '('; }
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

static inline int parse_midinote(const char *s, const char *e, int allow_p_prefix) {
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

static int parse_op(Parser *p, int left_node, char op, int node_type, int optional_right) {
    if (left_node < 0)
        return -1;
    consume(p, op);
    int right_node;
    int ch = peek(p);
    if (optional_right && (isdelimiter(ch) || isop(ch))) {
        right_node = -1;
    } else {
        right_node = parse_expr(p);
        p->nodes[left_node].next_sib = right_node;
    }
    int parent_node = make_node(p, node_type, left_node, -1, p->nodes[left_node].start, p->i);
    return parent_node;
}

static int parse_euclid(Parser *p, int node) {
    consume(p, '(');
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
    int euclid_node = make_node(p, N_EUCLID, euclid_node0, -1, p->nodes[node].start, p->i);
    p->nodes[euclid_node0].next_sib = euclid_node1;
    p->nodes[euclid_node1].next_sib = euclid_node2;
    return euclid_node;
}

static int parse_expr(Parser *p) {
    int node = parse_expr_inner(p);
    skipws(p);
    if (node < 0)
        return -1;
    while (!isdelimiter(peek(p))) {
        int old_pos = p->i;
        switch (peek(p)) {
        case '_': // shorthand for elongate. we only allow it to be a single character
            if (isdelimiter(peek_i(p, p->i + 1))) {
                // ok count the _ tokens!
                int count = 1;
                while (peek(p) == '_' && isdelimiter(peek_i(p, p->i + 1))) {
                    consume(p, '_');
                    skipws(p);
                    count++;
                }
                assert(count >= 2);
                int elongate_node = make_node(p, N_ELONGATE, node, -1, p->nodes[node].start, p->i);
                p->nodes[elongate_node].value.number = count;
                node = elongate_node;
                break;
            }
            break;
        case '*':
            node = parse_op(p, node, '*', N_TIMES, 0);
            break;
        case ':':
            node = parse_op(p, node, ':', N_IDX, 0);
            break;
        case '/':
            node = parse_op(p, node, '/', N_DIVIDE, 0);
            break;
        case '?':
            node = parse_op(p, node, '?', N_DEGRADE, 1);
            break;
        case '!':
            node = parse_op(p, node, '!', N_REPLICATE, 0);
            break;
        case '@':
            node = parse_op(p, node, '@', N_ELONGATE, 0);
            break;
        case '(':
            node = parse_euclid(p, node);
            break;
        case '%':
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
            break;
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
    if (n->type == N_REPLICATE || n->type == N_ELONGATE) {
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

static Parser parse(const char *s, size_t n) {
    Parser p = {s, n};
    p.root = parse_args(&p, N_FASTCAT, 0, 0);
    if (p.root >= 0 && p.nodes[p.root].first_child >= 0 && p.nodes[p.nodes[p.root].first_child].next_sib < 0)
        p.root = p.nodes[p.root].first_child; // if the outermost group is just a single node, rip off the group node.
    update_lengths(&p, p.root);
    return p;
}

static inline void print_haps(Parser *p, Hap *haps, int n) {
    for (int i = 0; i < n; i++) {
        Hap h = haps[i];
        Node *n = p->nodes + h.node;
        printf("%f %f - node %d \"%.*s\":%d v=%g\n", h.t0, h.t1, h.node, n->end - n->start, p->s + n->start, h.sound_idx,
               n->value.number);
    }
}

static Hap *make_haps(Parser *p, int nodeidx, float t0, float t1, float tscale, float tofs, Hap **haps,
                      int dont_bother_with_retrigs_for_leaves, int sound_idx, float note_prob) {
    if (nodeidx < 0)
        return *haps;
    Node *n = p->nodes + nodeidx;
    if (n->type == N_LEAF && dont_bother_with_retrigs_for_leaves) {
        Hap h = (Hap){t0 * tscale + tofs, t1 * tscale + tofs, nodeidx, sound_idx};
        stbds_arrput(*haps, h);
        return *haps;
    }
    switch (n->type) {
    case N_DEGRADE:
    case N_IDX:
    case N_DIVIDE:
    case N_TIMES: {
        if (n->first_child < 0)
            return *haps;
        Node *nleft = p->nodes + n->first_child;
        if (nleft->next_sib >= 0) {
            // create new haps on the right side. then iterate over them, one at a time, and query haps for the left scaled
            // appropriately.
            Hap *right_haps = NULL;
            make_haps(p, nleft->next_sib, t0, t1, 1.f, 0.f, &right_haps, 1, 0, 1.f);
            // printf("queried right haps from %g to %g, got %d haps back\n", t0, t1, (int)stbds_arrlen(right_haps));
            // print_haps(p, right_haps, stbds_arrlen(right_haps));
            for (int i = 0; i < stbds_arrlen(right_haps); i++) {
                Hap *right_hap = right_haps + i;
                float speed_scale = 1.f;
                float degrade_amount = 0.f;
                float right_value = p->nodes[right_hap->node].value.number;
                if (n->type == N_DIVIDE || n->type == N_TIMES)
                    speed_scale = right_value;
                if (speed_scale <= 0.f)
                    speed_scale = 1.f;
                if (n->type == N_DIVIDE)
                    speed_scale = 1.f / speed_scale;
                else if (n->type == N_IDX)
                    sound_idx = (int)right_value;
                else if (n->type == N_DEGRADE)
                    degrade_amount = right_value;
                make_haps(p, n->first_child, right_hap->t0 * speed_scale, right_hap->t1 * speed_scale, tscale / speed_scale, tofs,
                          haps, 0, sound_idx, note_prob * (1.f - degrade_amount));
            }
            stbds_arrfree(right_haps);
        } else {
            // no value? proxy to the left. for degrade, if there is no right value, we assume 50% default.
            make_haps(p, n->first_child, t0, t1, tscale, tofs, haps, 0, sound_idx,
                      n->type == N_DEGRADE ? note_prob * 0.5f : note_prob);
        }
        return *haps;
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
                // if (from < t1 && to > t0) {
                if (from >= t0 && from < t1) {
                    // this child overlaps the query range
                    float child_from = loop_index * child_length;
                    float child_to = child_from + child_length;
                    make_haps(p, child, child_from, child_to, tscale, tofs + (from - child_from) * tscale, haps, 0, sound_idx,
                              note_prob);
                }
                from = to;
                child = child_n->next_sib;
                if (child < 0) {
                    child = n->first_child;
                    loop_index++;
                }
            } else { // leaf
                float to = from + 1.f;
                // if (from < t1 && to > t0) {
                if (from >= t0 && from < t1) {
                    if (note_prob >= 1.f || rnd01() <= note_prob) {
                        if (nodeidx < 0 || !is_rest(p->nodes[nodeidx].value.sound->name)) {
                            Hap h = (Hap){from * tscale + tofs, to * tscale + tofs, nodeidx, sound_idx};
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
    case N_PARALLEL: {
        int child = n->first_child;
        while (child >= 0) {
            make_haps(p, child, t0, t1, tscale, tofs, haps, 0, sound_idx, note_prob);
            child = p->nodes[child].next_sib;
        }
        break;
    }
    }
    return *haps;
}
void test_minipat(void) {
    // const char *s = "sd,<oh hh>,[[bd bd:1 -] rim]";
    const char *s = "[[rim?]*8]";
    // const char *s = "{c eb g, c2 g2}%4";
    // const char *s = "[bd <hh oh>,rim*<4 8>]";
    // const char *s = "[bd,sd*1.1]";
    printf("\nparsing " COLOR_BRIGHT_GREEN "\"%s\"\n" COLOR_RESET "\n", s);
    Parser p = parse(s, strlen(s));
    pretty_print(s, p.nodes, p.root, 0);
    Hap *haps = NULL;
    make_haps(&p, p.root, 0.0f, 4.0f, 1.0f, 0.f, &haps, 0, 0, 1.f);
    printf(
        "        time 0   |   |   |   v   |   |   |   1   |   |   |   v   |   |   |   2   |   |   |   v   |   |   |   3   |   |   "
        "|   v   |   |   |   4\n");
    for (int i = 0; i < stbds_arrlen(haps); i++) {
        Node *n = &p.nodes[haps[i].node];
        int j;
        for (j = 0; j < i; ++j) {
            Node *nj = &p.nodes[haps[j].node];
            if (n->value.sound == nj->value.sound && haps[j].sound_idx == haps[i].sound_idx)
                break;
        }
        if (j < i)
            continue;
        printf("%10s:%d ", n->value.sound ? n->value.sound->name : "", haps[i].sound_idx);
        char line[129] = {};
        for (int j = 0; j < 128; ++j)
            line[j] = '.';
        for (int j = i; j < stbds_arrlen(haps); ++j) {
            Node *nj = &p.nodes[haps[j].node];
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
        printf("%s\n", line);
    }
}
