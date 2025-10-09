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
#include "utils.h"
#include "miniparse.h"
#include "ansicols.h"

const static char *param_enum_names[P_LAST] = {
#define X(x, ...) #x,
#include "params.h"
};

const static char *param_names[P_LAST] = {
#define X(x, shortname, ...) shortname,
#include "params.h"
};

enum {
#include "node_types.h"
    N_LAST
};

static const char *node_type_names[N_LAST] = {
#define NODE(x, ...) #x,
#include "node_types.h"
};

static inline uint32_t hash2_pcg(uint32_t a, uint32_t b) {
    const uint64_t MUL = 6364136223846793005ull;
    uint64_t state = ((uint64_t)a << 32) | b; // pack inputs
    uint64_t inc = ((uint64_t)b << 1) | 1ull; // odd stream

    state = state * MUL + inc; // LCG step

    uint32_t xorshifted = (uint32_t)(((state >> 18) ^ state) >> 27);
    uint32_t rot = (uint32_t)(state >> 59);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

// kinda base64, but we depart to make some punctiation more 'useful'.
// . and _ look low, so they are 1 and 0
// ^ looks high, so we go 64. @ looks full, and # is hard to type on UK keyboards, but both are also 64
// = looks middle-y so we make that 32.
// thus you can interpolate key values with _ = ^
const char btoa_tab[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789$@";

//clang-format off
const uint8_t atob_tab[256] =
    {
        [' '] = 0,  ['_'] = 0,  ['.'] = 0,  ['='] = 32, ['A'] = 0,  1,          2,          3,          4,          5,  6,
        7,          8,          9,          10,         11,         12,         13,         14,         15,         16, 17,
        18,         19,         20,         21,         22,         23,         24,         25,         ['a'] = 26, 27, 28,
        29,         30,         31,         32,         33,         34,         35,         36,         37,         38, 39,
        40,         41,         42,         43,         44,         45,         46,         47,         48,         49, 50,
        51,         ['0'] = 52, 53,         54,         55,         56,         57,         58,         59,         60, 61,
        ['!'] = 32, ['+'] = 62, ['-'] = 62, ['*'] = 62, ['/'] = 63, ['$'] = 63, ['#'] = 64, ['@'] = 64, ['^'] = 64,
};

//clang-format on

Sound *get_sound_init_only(const char *name) { // ...by name.
    Sound *sound = shget(G->sounds, name);
    if (!sound) {
        name = strdup(name); // intern the name!
        sound = (Sound *)calloc(1, sizeof(Sound));
        sound->name = name;
        shput(G->sounds, name, sound);
    }
    return sound;
}

Sound *add_alias_init_only(const char *alias, const char *name) {
    Sound *point_at_sound = get_sound_init_only(name);
    Sound *alias_sound = shget(G->sounds, alias);
    if (!alias_sound) {
        alias = strdup(alias); // intern the name!
        shput(G->sounds, alias, point_at_sound);
        alias_sound = point_at_sound;
    }
    return alias_sound;
}

const char *print_midinote(int note) {
    static char buf[4];
    if (note < 0 || note >= 128)
        return "";
    const static char notenames[12] = {'c', 'c', 'd', 'd', 'e', 'f', 'f', 'g', 'g', 'a', 'a', 'b'};
    const static char notesharps[12] = {' ', '#', ' ', '#', ' ', ' ', '#', ' ', '#', ' ', '#', ' '};
    buf[0] = notenames[note % 12];
    buf[1] = notesharps[note % 12];
    buf[2] = 0;
    buf[3] = 0;
    buf[(buf[1] == ' ') ? 1 : 2] = note / 12 + '0';
    return buf;
}

void pretty_print_haps(HapSpan haps, hap_time from, hap_time to) {
    for (Hap *hap = haps.s; hap < haps.e; hap++) {
        // hap onset must overlap the query range
        if (hap->t0 < from || hap->t0 >= to)
            continue;
        printf(COLOR_GREY "%08x(%d) " COLOR_CYAN "%5.2f" COLOR_BLUE " -> %5.2f " COLOR_RESET, hap->hapid, hap->node,
               hap->t0 / double(hap_cycle_time), hap->t1 / double(hap_cycle_time));
        for (int i = 0; i < P_LAST; i++) {
            if (hap->valid_params & (1 << i)) {
                if (i == P_SOUND) {
                    Sound *sound = get_sound_by_index((int)hap->params[i]);
                    printf("s=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", sound ? sound->name : "<missing>");
                } else if (i == P_NOTE) {
                    printf("note=" COLOR_BRIGHT_YELLOW "%s" COLOR_RESET " ", print_midinote((int)hap->params[i]));
                } else {
                    printf("%s=" COLOR_BRIGHT_YELLOW "%5.2f" COLOR_RESET " ", param_names[i], hap->params[i]);
                }
            }
        }
        printf("\n");
    }
}

void pretty_print_nodes(const char *src, const char *srcend, Node *nodes, int i, int depth) {
    if (i < 0)
        return;
    int c0 = nodes[i].start;
    int c1 = nodes[i].end;
    printf(COLOR_GREY "%d " COLOR_BLUE "%.*s" COLOR_BRIGHT_YELLOW "%.*s" COLOR_BLUE "%.*s" COLOR_RESET, i, c0, src, c1 - c0,
           src + c0, (int)(srcend - src - c1), src + c1);
    for (int j = 0; j < depth + 1; j++) {
        printf("  ");
    }
    switch (nodes[i].value_type) {
    case VT_SOUND: {
        Sound *sound = get_sound_by_index((int)nodes[i].max_value);
        printf("%d - %s - maxlen %g val %g-%g %s\n", i, node_type_names[nodes[i].type], nodes[i].total_length, nodes[i].min_value,
               nodes[i].max_value, sound ? sound->name : "");
        break;
    }
    case VT_NOTE: {
        printf("%d - %s - maxlen %g val %g-%g %s-%s\n", i, node_type_names[nodes[i].type], nodes[i].total_length,
               nodes[i].min_value, nodes[i].max_value, print_midinote((int)nodes[i].min_value),
               print_midinote((int)nodes[i].max_value));
        break;
    }
    default:
        printf("%d - %s - maxlen %g val %g-%g\n", i, node_type_names[nodes[i].type], nodes[i].total_length, nodes[i].min_value,
               nodes[i].max_value);
        break;
    }
    if (nodes[i].first_child >= 0) {
        pretty_print_nodes(src, srcend, nodes, nodes[i].first_child, depth + 1);
    }
    if (nodes[i].next_sib >= 0) {
        pretty_print_nodes(src, srcend, nodes, nodes[i].next_sib, depth);
    }
}

static void error(PatternMaker *p, const char *msg) {
    if (p->errmsg != NULL)
        return;
    p->err = p->i;
    p->errmsg = msg;
    // fprintf(stderr, "ERROR: %s - %.*s" COLOR_BRIGHT_RED "%s" COLOR_RESET "\n", msg, p->err, p->s, p->s + p->err);
}

static void skipws(PatternMaker *p) {
    while (1) {
        while (p->i < p->n && isspace(p->s[p->i]))
            p->i++;
        if (p->i + 2 > p->n || p->s[p->i] != '/' || p->s[p->i + 1] != '/')
            return;
        // skip // comment
        p->i += 2;
        while (p->i < p->n && p->s[p->i] != '\n')
            p->i++;
    }
}

static int peek(PatternMaker *p) { return (p->i >= p->n) ? -1 : (unsigned char)p->s[p->i]; }
static int peek_i(PatternMaker *p, int i) { return (i >= p->n) ? -1 : (unsigned char)p->s[i]; }

static int consume(PatternMaker *p, int c) {
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
static int make_node(PatternMaker *p, int node_type, int first_child, int next_sib, int start, int end) {
    int i = stbds_arrlen(p->nodes);
    Node n = (Node){.type = (uint8_t)node_type,
                    .start = start,
                    .end = end,
                    .first_child = first_child,
                    .next_sib = next_sib,
                    .total_length = 0};
    stbds_arrput(p->nodes, n);
    return i;
}

static int parse_expr(PatternMaker *p);

static int parse_args(PatternMaker *p, int group_type, int start, int is_poly) {
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

static int parse_group(PatternMaker *p, char open, char close, int node_type) {
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

int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix) {
    if (allow_p_prefix && e > s + 2 && s[0] == 'P' && s[1] == '_') {
        s += 2;
    }
    if (s >= e)
        return -1;
    char notename = s[0];
    if (notename >= 'A' && notename <= 'Z')
        notename += 'a' - 'A';
    if (notename < 'a' || notename > 'g')
        return -1;
    const static int note_indices[7] = {9, 11, 0, 2, 4, 5, 7};
    int note = note_indices[notename - 'a'];
    int octave = 3;
    s++;
    if (s < e) {
        if (s[0] == 's' || s[0] == '#') {
            note++;
            s++;
        } else if (s[0] == 'b') {
            note--;
            s++;
        }
    }
    if (s < e && s[0] >= '0' && s[0] <= '9') {
        octave = *s++ - '0';
    }
    if (!end && s != e)
        return -1;
    if (end)
        *end = s;
    return note + octave * 12;
}

static int parse_number(const char *s, const char *e, const char **end, float *number) {
    if (e <= s)
        return 0;
    const char *check = s;
    if (*check == '-')
        check++;
    if (check >= e || !isdigit(*check))
        return 0;
    char buf[e - s + 1];
    memcpy(buf, s, e - s);
    buf[e - s] = '\0';
    char *endptr = (char *)e;
    double d = strtod(buf, &endptr);
    if (end)
        *end = s + (endptr - buf);
    else if (endptr != buf + (e - s))
        return 0;
    *number = (float)d;
    return 1;
}

static inline EValueType parse_number_or_note_or_sound(const char *s, const char *e, float *out) {
    int note = parse_midinote(s, e, 0, 0);
    if (note >= 0) {
        *out = note;
        return VT_NOTE;
    } else if (parse_number(s, e, 0, out)) {
        return VT_NUMBER;
    }
    int sound_idx = get_sound_index(temp_cstring_from_span(s, e));
    *out = sound_idx;
    return VT_SOUND;
}

static inline EValueType parse_value(const char *s, const char *e, float *minval, float *maxval) {
    if (s == e)
        return VT_NONE;
    const char *dash = s + 1;
    while (dash < e && *dash != '-')
        dash++;
    EValueType type1 = parse_number_or_note_or_sound(s, dash, minval);
    if (type1 == VT_NONE)
        return VT_NONE;
    if (dash != e) {
        EValueType type2 = parse_number_or_note_or_sound(dash + 1, e, maxval);
        if (type2 != type1)
            return VT_NONE;
    } else
        *maxval = *minval;
    return type1;
}

void fill_curve_data_from_string(float *data, const char *s, int n) {
    float last_val = 0.f;
    float dlast_val = 0.f;
    for (int i = 0; i < n; i++) {
        last_val += dlast_val;
        data[i] = last_val;
        if (s[i] != ' ') {
            data[i] = atob_tab[s[i]] / 64.f;
            last_val = dlast_val = 0.f;
            // look for a next datapoint to interpolate towards
            for (int j = i + 1; j < n; j++) {
                if (s[j] != ' ') {
                    last_val = data[i];
                    float next_val = atob_tab[s[j]] / 64.f;
                    dlast_val = (next_val - last_val) / (j - i);
                    break;
                }
            }
        }
    }
}

static int parse_curve(PatternMaker *p) {
    int start = p->i;
    consume(p, '\'');
    while (p->i < p->n && p->s[p->i] != '\'')
        ++p->i;
    if (!consume(p, '\'')) {
        error(p, "expected closing single quote in curve");
        return -1;
    }
    int node = make_node(p, N_CURVE, -1, -1, start, p->i);
    int first_data_idx = stbds_arrlen(p->curvedata);
    int num_data_idx = max(0, p->i - start - 2);
    float *data = stbds_arraddnptr(p->curvedata, num_data_idx);
    fill_curve_data_from_string(data, p->s + start + 1, num_data_idx);
    p->nodes[node].min_value = first_data_idx;
    p->nodes[node].max_value = first_data_idx + num_data_idx;
    return node;
}

static int parse_leaf(PatternMaker *p) {
    int start = p->i;
    while (isleaf(peek(p)))
        p->i++;
    // split the string into parts separated by _; valid parts can be a number, a note, or a sound.
    float minval, maxval;
    EValueType type = parse_value(p->s + start, p->s + p->i, &minval, &maxval);
    if (type == VT_NONE) {
        error(p, "expected value");
        return -1;
    }
    int node = make_node(p, N_LEAF, -1, -1, start, p->i);
    p->nodes[node].value_type = type;
    p->nodes[node].min_value = minval;
    p->nodes[node].max_value = maxval;
    return node;
}

static int parse_expr_inner(PatternMaker *p) {
    skipws(p);
    switch (peek(p)) {
    case '[':
        return parse_group(p, '[', ']', N_FASTCAT);
    case '<':
        return parse_group(p, '<', '>', N_CAT);
    case '{':
        return parse_group(p, '{', '}', N_POLY);
    case '\'':
        return parse_curve(p);
    default:
        if (isleaf(peek(p)))
            return parse_leaf(p);
    }
    error(p, "unexpected token");
    return -1;
}

static int parse_euclid(PatternMaker *p, int node) {
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

static int parse_op(PatternMaker *p, int left_node, int node_type, int num_params, int optional_right) {
    if (left_node < 0)
        return -1;
    if (node_type == N_OP_EUCLID) {
        return parse_euclid(
            p, left_node); // euclid node is different - it has a closing ) and comma separated args. because, idk. history.
    }
    if (!optional_right)
        skipws(p);
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

static int parse_expr(PatternMaker *p) {
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
                p->nodes[elongate_node].max_value = count;
                node = elongate_node;
                break;
            }
        } else if (peek(p) == '%') {
            if (p->nodes[node].type == N_POLY) {
                consume(p, '%');
                int modulo_node = parse_leaf(p);
                if (modulo_node < 0 || p->nodes[modulo_node].max_value <= 0.f) {
                    error(p, "expected positive number in modulo");
                    return -1;
                }
                // we store the modulo constant in our value.number. a bit of a hack...
                p->nodes[node].max_value = p->nodes[modulo_node].max_value;
            }
        } else if (peek(p) == '(') {
            consume(p, '(');
            node = parse_euclid(p, node);
        } else {
            int i = p->i;
            while (!isdelimiter(p->s[i]) && !isopening(p->s[i]) && !isdigit(p->s[i]))
                ++i;
            if (i > p->i) {
                uint32_t name_hash = literal_hash_span(p->s + p->i, p->s + i);
                switch (name_hash) {
#define NODE(x, ...)
#define OP(op_type, srcname, numparams, optional_right)                                                                            \
    case HASH(srcname):                                                                                                            \
        p->i = i;                                                                                                                  \
        node = parse_op(p, node, op_type, numparams, optional_right);                                                              \
        break;
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
template <typename T> float get_length(T *p, int node) {
    if (node < 0)
        return 1.f;
    Node *n = &p->nodes[node];
    if (n->type == N_OP_REPLICATE || n->type == N_OP_ELONGATE) {
        return n->max_value;
    }
    return 1.f;
}

static void update_lengths(PatternMaker *p, int node) {
    if (node < 0)
        return;
    Node *n = &p->nodes[node];
    float max_value = 0.f;
    float total_length = 0.f;
    if (n->first_child < 0)
        total_length = 1.f;
    for (int i = n->first_child; i >= 0; i = p->nodes[i].next_sib) {
        update_lengths(p, i);
        if (i != n->first_child)
            max_value = max(max_value, p->nodes[i].max_value);
        total_length += get_length(p, i);
    }
    n->total_length = total_length;
    if (n->type != N_POLY && n->type != N_LEAF) {
        n->max_value = max_value;
    }
}

static inline void squeeze(PatternMaker *p, int *parent) {
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

Pattern parse_pattern(PatternMaker *p) {
    p->i = 0;
    if (p->nodes)
        stbds_arrsetlen(p->nodes, 0);
    if (p->curvedata)
        stbds_arrsetlen(p->curvedata, 0);
    p->root = -1;
    p->err = 0;
    p->errmsg = NULL;
    p->root = parse_args(p, N_FASTCAT, 0, 0);
    update_lengths(p, p->root);
    squeeze(p, &p->root);
    return p->get_pattern();
}

static inline int cmp_haps_by_t0(const Hap *a, const Hap *b) { return a->t0 < b->t0 ? -1 : a->t0 > b->t0 ? 1 : 0; }

static inline void sort_haps_by_t0(HapSpan haps) {
    qsort(haps.s, haps.e - haps.s, sizeof(Hap), (int (*)(const void *, const void *))cmp_haps_by_t0);
}

static inline void init_hap_in_place(Hap *dst, hap_time t0, hap_time t1, int nodeidx, int hapid, int value_type, float value) {
    dst->t0 = t0;
    dst->t1 = t1;
    dst->node = nodeidx;
    dst->valid_params = 1 << value_type;
    dst->hapid = hapid;
    dst->params[value_type] = value;
}

HapSpan Pattern::_append_hap(HapSpan &dst, int nodeidx, hap_time t0, hap_time t1, int hapid) {
    if (dst.s >= dst.e)
        return {};
    Node *n = nodes + nodeidx;
    int value_type = n->value_type;
    if (value_type <= VT_NONE)
        return {};
    bool is_rest = value_type == VT_SOUND && n->max_value < 2;
    if (is_rest)
        return {};
    float v = n->max_value;
    if (n->min_value != n->max_value) { // randomize in range
        float t = (pcg_mix(pcg_next(hapid)) & 0xffffff) * (1.f / 0xffffff);
        v = lerp(n->min_value, n->max_value, t);
        if (value_type == VT_NOTE && n->max_value - n->min_value >= 1.f) // if it's a range of at least two semitone, quantize to
                                                                         // nearest semitone
            v = roundf(v);
    }
    init_hap_in_place(dst.s++, t0, t1, nodeidx, hapid, value_type, v);
    return {dst.s - 1, dst.s};
}

static inline hap_time global_to_local(hap_time t, fraction_t tscale, hap_time tofs) {
    return scale_time(t - tofs, tscale.inverse());
}
static inline hap_time local_to_global(hap_time t, fraction_t tscale, hap_time tofs) { return scale_time(t, tscale) + tofs; }

// consumes dst; returns a new span of added haps, either with dst or empty.
HapSpan Pattern::_make_haps(HapSpan &dst, HapSpan &tmp, int nodeidx, hap_time global_t0, hap_time global_t1, fraction_t tscale,
                            hap_time tofs, int flags, int hapid) {
    if (nodeidx < 0 || global_t0 > global_t1)
        return {};
    Node *n = nodes + nodeidx;
    if (n->type == N_LEAF && (flags & FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES)) {
        return _append_hap(dst, nodeidx, global_t0, global_t1, hapid);
    }
    hapid = hash2_pcg(hapid, nodeidx); // go deeper in the tree...
    HapSpan rv = {dst.s, dst.s};
    Node *first_child = (n->first_child < 0) ? NULL : nodes + n->first_child;
    switch (n->type) {
    case N_OP_EUCLID: {
        if (!first_child)
            return {};
        int setsteps_nodeidx = first_child->next_sib;
        if (setsteps_nodeidx < 0)
            return {};
        int numsteps_nodeidx = nodes[setsteps_nodeidx].next_sib;
        if (numsteps_nodeidx < 0)
            return {};
        int rot_nodeidx = nodes[numsteps_nodeidx].next_sib;
        HapSpan setsteps_haps = _make_haps(tmp, tmp, setsteps_nodeidx, global_t0, global_t1, tscale, tofs,
                                           FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 1);
        HapSpan numsteps_haps = _make_haps(tmp, tmp, numsteps_nodeidx, global_t0, global_t1, tscale, tofs,
                                           FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 2);
        HapSpan rot_haps = _make_haps(tmp, tmp, rot_nodeidx, global_t0, global_t1, tscale, tofs,
                                      FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 3);
        sort_haps_by_t0(setsteps_haps);
        sort_haps_by_t0(numsteps_haps);
        sort_haps_by_t0(rot_haps);
        hap_time from = global_t0;
        while (from < global_t1) {
            while (setsteps_haps.hasatleast(2) && setsteps_haps.s[1].t0 <= from)
                setsteps_haps.s++;
            while (numsteps_haps.hasatleast(2) && numsteps_haps.s[1].t0 <= from)
                numsteps_haps.s++;
            while (rot_haps.hasatleast(2) && rot_haps.s[1].t0 <= from)
                rot_haps.s++;
            hap_time to = global_t1;
            to = min(to, setsteps_haps.hasatleast(2) ? setsteps_haps.s[1].t0 : global_t1);
            to = min(to, numsteps_haps.hasatleast(2) ? numsteps_haps.s[1].t0 : global_t1);
            to = min(to, rot_haps.hasatleast(2) ? rot_haps.s[1].t0 : global_t1);
            if (to <= from)
                break;
            int setsteps = setsteps_haps.empty() ? 0 : nodes[setsteps_haps.s[0].node].max_value;
            int numsteps = numsteps_haps.empty() ? 1 : nodes[numsteps_haps.s[0].node].max_value;
            int rot = rot_haps.empty() ? 0 : nodes[rot_haps.s[0].node].max_value;
            if (numsteps <= 0)
                break;
            tscale /= numsteps;
            hap_time child_t0 = global_to_local(from, tscale, tofs);
            hap_time child_t1 = global_to_local(to, tscale, tofs);
            int stepidx = floor2cycle(child_t0);
            while (child_t0 < child_t1) {
                if (euclid_rhythm(haptime2cycleidx(stepidx), setsteps, numsteps, rot)) {
                    hap_time step_from = local_to_global(stepidx, tscale, tofs);
                    hap_time step_to = local_to_global(stepidx + hap_cycle_time, tscale, tofs);
                    _make_haps(dst, tmp, n->first_child, max(global_t0, step_from), min(global_t1, step_to), tscale, tofs, flags,
                               hapid + haptime2cycleidx(stepidx));
                }
                child_t0 = (stepidx += hap_cycle_time);
            }
            from = to;
        }
        break;
    }
    case N_OP_DEGRADE:
    case N_OP_IDX:
    case N_OP_REPLICATE:
    case N_OP_ELONGATE:
    case N_OP_DIVIDE:
    case N_OP_TIMES: {
        float right_value = 0.5f;
        HapSpan right_haps = {};
        int num_right_haps = 1;
        if (first_child->next_sib >= 0) {
            right_haps = _make_haps(tmp, tmp, first_child->next_sib, global_t0, global_t1, tscale, tofs,
                                    FLAG_DONT_BOTHER_WITH_RETRIGS_FOR_LEAVES, hapid + 1);
            num_right_haps = right_haps.e - right_haps.s;
        }
        int target_type = (n->type == N_OP_IDX) ? P_NUMBER : -1;
        for (int i = 0; i < num_right_haps; i++) {
            if (!right_haps.empty()) {
                Hap *right_hap = right_haps.s + i;
                if (!(right_hap->valid_params & (1 << P_NUMBER)))
                    continue; // TODO  does it ever make sense for the right side to have non-number data?
                right_value = right_hap->params[P_NUMBER];
            }
            if (n->type == N_OP_TIMES) {
                if (right_value <= 0.f)
                    continue;
                tscale /= right_value;
            } else if (n->type == N_OP_DIVIDE || n->type == N_OP_ELONGATE) {
                if (right_value <= 0.f)
                    continue;
                tscale *= (right_value ? right_value : 1.f);
            }
            hap_time child_t0 = !right_haps.empty() ? right_haps.s[i].t0 : global_t0;
            hap_time child_t1 = !right_haps.empty() ? right_haps.s[i].t1 : global_t1;
            child_t0 = max(global_t0, child_t0);
            child_t1 = min(global_t1, child_t1);
            HapSpan child_haps = _make_haps(dst, tmp, n->first_child, child_t0, child_t1, tscale, tofs, flags, hapid + i);
            if (n->type == N_OP_DEGRADE) {
                // blank out degraded haps
                if (right_value >= 1.f)
                    dst.e = rv.e; // delete all outputs!
                else if (right_value > 0.f)
                    for (Hap *hap = child_haps.s; hap < child_haps.e; hap++) {
                        float t = (pcg_mix(pcg_next(23124 - hap->hapid)) & 0xffffff) * (1.f / 0xffffff);
                        if (right_value >= t) {
                            hap->valid_params = 0; // mark it as invalid basically
                        }
                    }
            } else if (target_type >= 0) {
                for (Hap *hap = child_haps.s; hap < child_haps.e; hap++) {
                    hap->params[target_type] = right_value;
                    hap->valid_params |= 1 << target_type;
                    // todo update hap id?
                }
            }
        }
        break;
    }
    case N_RANDOM: {
        int num_children = 0;
        for (int i = n->first_child; i >= 0; i = nodes[i].next_sib)
            ++num_children;
        if (!num_children)
            return {};
        int kids[num_children];
        num_children = 0;
        for (int i = n->first_child; i >= 0; i = nodes[i].next_sib)
            kids[num_children++] = i;
        hap_time t0 = global_to_local(global_t0, tscale, tofs);
        hap_time t1 = global_to_local(global_t1, tscale, tofs);
        int stepidx = floor2cycle(t0);
        hap_time from = t0;
        while (from < t1) {
            int kid = kids[hash2_pcg(hapid, haptime2cycleidx(stepidx)) % num_children];
            hap_time child_t0 = max(global_t0, local_to_global(stepidx, tscale, tofs));
            hap_time child_t1 = min(global_t1, local_to_global(stepidx + hap_cycle_time, tscale, tofs));
            _make_haps(dst, tmp, kid, child_t0, child_t1, tscale, tofs, flags, hapid + kid);
            from = (stepidx += hap_cycle_time);
        }
        break;
    }
    case N_FASTCAT:
        if (n->total_length <= 0.f)
            break;
        tscale /= n->total_length;
    case N_LEAF:
    case N_CAT: {
        hap_time total_length = hap_time(n->total_length * hap_cycle_time);
        hap_time t0 = global_to_local(global_t0, tscale, tofs);
        hap_time t1 = global_to_local(global_t1, tscale, tofs);
        int loop_index = total_length > 0 ? t0 / total_length : 0;
        int child = n->first_child;
        hap_time from = loop_index * total_length;
        while (from < t1) {
            if (child >= 0) {
                Node *child_n = nodes + child;
                hap_time child_length = (hap_time)(get_length(this, child) * hap_cycle_time);
                if (child_length <= 0)
                    child_length = hap_cycle_time;
                hap_time to = from + child_length;
                if (to > t0 && from < t1) {
                    // this child overlaps the query range
                    hap_time child_from = loop_index * child_length;
                    hap_time child_to = (loop_index + 1) * child_length;
                    _make_haps(dst, tmp, child, max(global_t0, local_to_global(from, tscale, tofs)),
                               min(global_t1, local_to_global(to, tscale, tofs)), tscale,
                               tofs + scale_time(from - child_from, tscale), flags, hapid + (int)(loop_index * 999) + child);
                }
                from = to;
                child = child_n->next_sib;
                if (child < 0) {
                    child = n->first_child;
                    loop_index++;
                }
            } else { // leaf
                hap_time to = from + hap_cycle_time;
                if (to > t0 && from < t1) {
                    if (nodeidx >= 0 && nodes[nodeidx].value_type > VT_NONE) {
                        // we do NOT clip this one, so that we know when the notedown was!
                        // but we do skip it entirely if its outside the query range,
                        // and we check with an epsilon in *global* time.
                        hap_time t0g = local_to_global(from, tscale, tofs);
                        hap_time t1g = local_to_global(to, tscale, tofs);
                        if (t1g > global_t0 + epsilon && t0g < global_t1 - epsilon)
                            _append_hap(dst, nodeidx, t0g, t1g, hapid + (int)(loop_index * 999) + nodeidx);
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
            speed_scale = (n->max_value > 0) ? n->max_value : (child >= 0) ? nodes[child].total_length : 1.;
        }
        int childidx = 0;
        if (speed_scale < 0.f)
            break;
        tscale /= speed_scale;
        while (child >= 0) {
            _make_haps(dst, tmp, child, global_t0, global_t1, tscale, tofs, flags, hapid + childidx);
            child = nodes[child].next_sib;
            ++childidx;
        }
        break;
    }
    }
    rv.e = dst.s;
    return rv;
}

void test_minipat(void) {
    // const char *s = "sd,<oh hh>,[[bd bd:1 -] rim]";
    // const char *s = "{bd sd, rim hh oh}%4";
    // const char *s = "[bd | sd | rim]*8";
    // const char *s = "[[sd] [bd]]"; // test squeeze
    // const char *s = "[sd*<2 1> bd(<3 1 4>,8)]"; // test euclid
    const char *s = "[- bd sd c4*4 23-25]"; // test divide
    // const char *s = "<bd sd>";
    //  const char *s = "{c eb g, c2 g2}%4";
    //  const char *s = "[bd <hh oh>,rim*<4 8>]";
    //  const char *s = "[bd,sd*1.1]";
    printf("\nparsing " COLOR_BRIGHT_GREEN "\"%s\"\n" COLOR_RESET "\n", s);
    PatternMaker pm = {.s = s, .n = (int)strlen(s)};
    parse_pattern(&pm);
    Pattern p = pm.get_pattern();
    printf("parsed %d nodes\n", (int)stbds_arrlen(p.nodes));
    if (pm.errmsg)
        printf("error: %s\n", pm.errmsg);
    pretty_print_nodes(s, s + pm.n, p.nodes, p.root, 0);
    Hap tmp[64], dst[64];
    HapSpan haps = p.make_haps({dst, dst + 64}, {tmp, tmp + 64}, 0.f, 4.f);
    pretty_print_haps(haps, 0.f, 4.f);

    // char *chart = print_pattern_chart(&p);
    // printf("%s\n", chart);
    // stbds_arrfree(chart);
}

int ispathchar(char c) { return isalnum(c) || c == '_' || c == '-' || c == '.'; }

const char *skip_path(const char *s, const char *e) {
    while (s < e) {
        if (*s != '/')
            return s;
        ++s; // skip /
        while (s < e && (ispathchar(*s)))
            ++s;
    }
    return s;
}

const char *find_end_of_pattern(const char *s, const char *e) {
    // look for a blank line, or a line that starts with / or #
    while (s < e) {
        while (s < e && *s != '\n')
            ++s; // find new line
        if (s < e)
            ++s; // skip \n
        if (s + 2 < e && *s == '/' && s[1] != '/')
            return s;                  // its a new path
        while (s < e && isspace(*s)) { // skip leading white space
            if (*s == '\n')
                return s; // its a blank line
            ++s;          // skip white space
        }
        if (*s == '#')
            return s; // its a macro at the start of a line
    }
    return s;
}

const char *spanstr(const char *s, const char *e, const char *substr) {
    int n = strlen(substr);
    while (s < e) {
        while (s < e && *s != *substr)
            ++s;
        if (s + n > e)
            return NULL;
        if (strncmp(s, substr, n) == 0)
            return s;
        ++s;
    }
    return NULL;
}

void parse_named_patterns_in_c_source(const char *s, const char *real_e) {
    Pattern *patterns_map = NULL; // stbds_hm
    while (s < real_e) {
        s = spanstr(s, real_e, "#ifdef PATTERNS");
        if (!s)
            break;
        s += 15;
        const char *e = spanstr(s, real_e, "#endif");
        if (!e)
            break;
        while (s < e) {
            while (s < e && *s != '\n')
                ++s; // find end of line
            if (s < e)
                ++s; // skip \n
            const char *pathstart = s;
            if (s >= e || *s != '/' || (s + 1 < e && s[1] == '/'))
                continue; // not a path
            s = skip_path(s, e);
            const char *pathend = s;
            if (pathend == pathstart)
                continue; // not a path
            const char *pattern_start = s;
            s = find_end_of_pattern(s, e);
            if (s == pattern_start)
                continue; // not a pattern
            // printf("found pattern: %.*s - %.*s\n", (int)(pathend-pathstart), pathstart, (int)(s-pattern_start), pattern_start);
            PatternMaker p = {.s = pattern_start, .n = (int)(s - pattern_start)};
            Pattern pat = parse_pattern(&p);
            if (p.err <= 0) {
                printf("found pattern: %.*s\n", (int)(pathend - pathstart), pathstart);
                pretty_print_nodes(pattern_start, s - 1, pat.nodes, pat.root, 0);
                pat.key = make_cstring_from_span(pathstart, pathend, 0);
                stbds_hmputs(patterns_map, pat);
                Hap dst[64], tmp[64];
                HapSpan haps = pat.make_haps({dst, dst + 64}, {tmp, tmp + 64}, 0.f, 4.f * hap_cycle_time);
                pretty_print_haps(haps, 0.f, 4.f * hap_cycle_time);

                // HapSpan haps = p.make_haps({dst,dst+64}, {tmp,tmp+64}, 0.f, 4.f);
                //  pretty_print_haps(haps);
                //  HapSpan hapspan = p.make_haps(p.haps, p.root, 0.f, 4.f, 1.f, 0.f, 0, 1);
                //  Hap *haps = NULL;
                //  stbds_arrsetlen(haps, p.haps.e - p.haps.s);
                //  memcpy(haps, p.haps.s, (p.haps.e - p.haps.s) * sizeof(Hap));
                //  pretty_print_haps(haps);
                //  stbds_arrfree(haps);
            } else {
                printf("error: %.*s: %s\n", (int)(pathend - pathstart), pathstart, p.errmsg);
                pat.unalloc();
            }
        }
    }
    // TODO - let the old pattern table leak because concurrency etc
    G->patterns_map = patterns_map;
}