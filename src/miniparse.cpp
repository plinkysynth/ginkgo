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
    const static char notesharps[12] = {' ', 's', ' ', 's', ' ', ' ', 's', ' ', 's', ' ', 's', ' '};
    buf[0] = notenames[note % 12];
    buf[1] = notesharps[note % 12];
    buf[2] = 0;
    buf[3] = 0;
    buf[(buf[1] == ' ') ? 1 : 2] = note / 12 + '0';
    return buf;
}

static void error(pattern_maker_t *p, const char *msg) {
    if (p->errmsg != NULL)
        return;
    p->err = p->i;
    p->errmsg = msg;
    // fprintf(stderr, "ERROR: %s - %.*s" COLOR_BRIGHT_RED "%s" COLOR_RESET "\n", msg, p->err, p->s, p->s + p->err);
}

static void skipws(pattern_maker_t *p) {
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

static int peek(pattern_maker_t *p) { return (p->i >= p->n) ? -1 : (unsigned char)p->s[p->i]; }
static int peek_i(pattern_maker_t *p, int i) { return (i >= p->n) ? -1 : (unsigned char)p->s[i]; }

static int consume(pattern_maker_t *p, int c) {
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
static int make_node(pattern_maker_t *p, int node_type, int first_child, int next_sib, int start, int end) {
    int i = stbds_arrlen(p->nodes);
    Node n = (Node){.type = (uint8_t)node_type,
                    .start = start,
                    .end = end,
                    .first_child = first_child,
                    .next_sib = next_sib,
                    .total_length = 0,
                    .num_children = 0};
    stbds_arrput(p->nodes, n);
    return i;
}

static int parse_expr(pattern_maker_t *p);

static int parse_args(pattern_maker_t *p, int group_type, int start, int is_poly) {
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

static int parse_leaf(pattern_maker_t *p);

static int parse_group(pattern_maker_t *p, char open, char close, int node_type) {
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
    if (is_poly) {
        skipws(p);
        if (consume(p, '%')) {
            int modulo_node = parse_leaf(p);
            if (modulo_node < 0 || p->nodes[modulo_node].max_value <= 0.f) {
                error(p, "expected positive number in modulo");
                return -1;
            }
            // we store the modulo constant in our value.number. a bit of a hack...
            p->nodes[group_node].max_value = p->nodes[modulo_node].max_value;
        }
    }
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
    int sound_idx;
    if (e == s + 1 && (*s == '-' || *s == '~'))
        sound_idx = 0;
    else if (e == s + 1 && (*s == 'X' || *s == 'x'))
        sound_idx = 1;
    else
        sound_idx = get_sound_index(temp_cstring_from_span(s, e));
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

static int parse_curve(pattern_maker_t *p) {
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

static int parse_leaf(pattern_maker_t *p) {
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

static int parse_expr_inner(pattern_maker_t *p) {
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

static int parse_euclid(pattern_maker_t *p, int node) {
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

static int parse_op(pattern_maker_t *p, int left_node, int node_type, int num_params, int optional_right) {
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

static int parse_expr(pattern_maker_t *p) {
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

static void update_lengths(pattern_maker_t *p, int node) {
    if (node < 0)
        return;
    Node *n = &p->nodes[node];
    float max_value = 0.f;
    float total_length = 0.f;
    int num_children = 0;
    if (n->first_child < 0)
        total_length = 1.f;
    for (int i = n->first_child; i >= 0; i = p->nodes[i].next_sib) {
        ++num_children;
        update_lengths(p, i);
        if (i != n->first_child)
            max_value = max(max_value, p->nodes[i].max_value);
        total_length += get_length(p, i);
    }
    n->total_length = total_length;
    n->num_children = num_children;
    if (n->type != N_POLY && n->type != N_LEAF && n->type != N_OP_ELONGATE) {
        n->max_value = max_value;
    }
}

pattern_t parse_pattern(pattern_maker_t *p) {
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
    return p->make_pattern();
}

#include "makehaps.h"

void test_minipat(void) {
    // const char *s = "sd,<oh hh>,[[bd bd:1 -] rim]";
    // const char *s = "{bd sd, rim hh oh}%4";
    // const char *s = "[bd | sd | rim]*8";
    // const char *s = "[[sd] [bd]]"; // test squeeze
    // const char *s = "[sd*<2 1> bd(<3 1 4>,8)]"; // test euclid
    const char *s = "[c4 _ _ _ _]"; // test divide
    // const char *s = "<bd sd>";
    //  const char *s = "{c eb g, c2 g2}%4";
    //  const char *s = "[bd <hh oh>,rim*<4 8>]";
    //  const char *s = "[bd,sd*1.1]";
    printf("\nparsing " COLOR_BRIGHT_GREEN "\"%s\"\n" COLOR_RESET "\n", s);
    pattern_maker_t pm = {.s = s, .n = (int)strlen(s)};
    parse_pattern(&pm);
    pattern_t p = pm.make_pattern();
    printf("parsed %d nodes\n", (int)stbds_arrlen(pm.nodes));
    if (pm.errmsg)
        printf("error: %s\n", pm.errmsg);
    pretty_print_nodes(s, s + pm.n, &p);
    hap_t tmp[64], dst[64];
    hap_span_t haps = p.make_haps({dst, dst + 64}, {tmp, tmp + 64}, 0.1f, 0.2f);
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
    while (s < e) {
        while (s < e && *s != '\n')
            ++s; // find new line
        if (s + 3 < e && s[0] == '\n' && s[1] == '/' && s[2] != '/')
            return s;                  // its a new path
        if (s + 2 < e && s[0] == '\n' && s[1] == '#')
            return s; // its a line starting '#'
        if (s<e) ++s;
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
    pattern_t *patterns_map = NULL; // stbds_hm
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
            ++s;     // skip \n
            const char *pathstart = s;
            if (s >= e || *s != '/' || (s + 1 < e && s[1] == '/')) {
                continue; // not a path
            }
            s = skip_path(s, e);
            const char *pathend = s;
            if (pathend == pathstart)
                continue; // not a path
            const char *pattern_start = s;
            s = find_end_of_pattern(s, e);
            if (s == pattern_start)
                continue; // not a pattern
            // printf("found pattern: %.*s - %.*s\n", (int)(pathend-pathstart), pathstart, (int)(s-pattern_start),
            // pattern_start);
            pattern_maker_t p = {.s = pattern_start, .n = (int)(s - pattern_start)};
            pattern_t pat = parse_pattern(&p);
            if (p.err <= 0) {
                printf("found pattern: %.*s\n", (int)(pathend - pathstart), pathstart);
                pat.key = stbstring_from_span(pathstart, pathend, 0);
                stbds_shputs(patterns_map, pat);
                /*
                pretty_print_nodes(pattern_start, s, &pat);
                hap_t dst[64], tmp[64];
                hap_span_t haps = pat.make_haps({dst, dst + 64}, {tmp, tmp + 64}, 0.f, 4.f);
                pretty_print_haps(haps, 0.f, 4.f);
                */
            } else {
                printf("error: %.*s: %s\n", (int)(pathend - pathstart), pathstart, p.errmsg);
                pat.unalloc();
            }
        }
    }
    // TODO - let the old pattern table leak because concurrency etc
    G->patterns_map = patterns_map;
}
