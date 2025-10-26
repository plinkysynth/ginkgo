#pragma once
#include "hash_literal.h"
#include "utils.h"
#include "miniparse.h"
#include <ctype.h>

void add_line(float p0x, float p0y, float p1x, float p1y, uint32_t col, float width);

int parse_midinote(const char *s, const char *e, const char **end, int allow_p_prefix);
const char *print_midinote(int note);
const char *spanstr(const char *s, const char *e, const char *substr);

int fbw, fbh; // current framebuffer size in pixels

typedef struct error_msg_t {
    int key;           // a line number
    const char *value; // a line of text (terminated by \n)
} error_msg_t;

typedef struct edit_op_t {
    int remove_start;
    int remove_end;
    char *insert_str; // strdup'd string
    int cursor_idx_after;
    int select_idx_after;
} edit_op_t;

typedef struct autocomplete_option_t {
    const char *key;
    int value;
    int xoffset;
    int matchlen;
} autocomplete_option_t;

typedef struct character_pos_t {
    uint32_t x : 10;
    uint32_t y : 21;
    uint32_t pattern_mode : 1;
} character_pos_t;

static_assert(sizeof(character_pos_t) == 4, "character_pos_t should be 4 bytes");

typedef struct EditorState {
    // all these arrays are stbds_arrs
    const char *fname;                           // name of the file
    char *str;                                   // the text
    int *new_idx_to_old_idx;                     // remapping from last saved version to this version
    character_pos_t *character_pos;              //
    edit_op_t *edit_ops;                         //
    autocomplete_option_t *autocomplete_options; //
    int autocomplete_index;
    int autocomplete_scroll_y;
    float autocomplete_show_after;
    int undo_idx;
    bool is_shader; // is this a shader file?
    bool mouse_hovering_chart;
    bool find_mode;
    bool cursor_in_pattern_area;
    bool mouse_dragging_chart;
    bool mouse_clicked_chart;
    char mouse_click_original_char;
    int mouse_click_original_char_x;
    float scroll_x, scroll_y;
    float scroll_target_x, scroll_target_y;
    int intscroll_x, intscroll_y; // how many lines we scrolled.
    int cursor_x;
    int cursor_y;
    int cursor_x_target;
    int cursor_idx;
    int select_idx;
    int num_lines;
    int max_width;
    int need_scroll_update;
    float prev_cursor_x;
    float prev_cursor_y;
    int font_width;
    int font_height;
    char *last_compile_log;
    error_msg_t *error_msgs;
    float click_fx;
    float click_fy;
    float click_slider_value;
    int click_down_idx; // where in the text we clicked down.
} EditorState;

// add_line but coords are text
void add_line(EditorState *E, float p0x, float p0y, float p1x, float p1y, uint32_t col, float width) {
    p0x = (p0x + E->intscroll_x) * E->font_width - E->scroll_x;
    p0y = (p0y + E->intscroll_y) * E->font_height - E->scroll_y;
    p1x = (p1x + E->intscroll_x) * E->font_width - E->scroll_x;
    p1y = (p1y + E->intscroll_y) * E->font_height - E->scroll_y;
    add_line(p0x, p0y, p1x, p1y, col, width);
}

void init_remapping(EditorState *E);

void parse_named_patterns_in_c_source(EditorState *E) {
    const char *s = E->str, *real_e = E->str + stbds_arrlen(E->str);
    init_remapping(E);
    parse_named_patterns_in_c_source(E->str, E->str + stbds_arrlen(E->str));
}

typedef struct slider_spec_t {
    int start_idx;
    int end_idx;
    int value_start_idx;
    int value_end_idx;
    float minval, maxval, curval;
} slider_spec_t;

void init_remapping(EditorState *E) {
    int n = stbds_arrlen(E->str);
    stbds_arrsetlen(E->new_idx_to_old_idx, n);
    for (int i = 0; i < n; i++) {
        E->new_idx_to_old_idx[i] = i;
    }
}

bool ispartofnumber(char c) { return isdigit(c) || c == '-' || c == '.' || c == 'e' || c == 'E'; }

static inline bool iswordbreak(char c) { return !isalnum(c) && !ispartofnumber(c); }
static inline bool isnewline(char c) { return c == '\n' || c == '\r'; }

int try_parse_number(const char *str, int n, int idx, float *out, int *out_idx, float default_val) {
    int i = idx;
    while (i < n && ispartofnumber(str[i]))
        ++i;
    if (i == idx) {
        *out = default_val;
        if (out_idx)
            *out_idx = idx;
        return 0;
    }
    char tmp[i - idx + 1];
    memcpy(tmp, str + idx, i - idx);
    tmp[i - idx] = '\0';
    char *end = tmp;
    float val = strtof(tmp, &end);
    if (end <= tmp) {
        *out = default_val;
        if (out_idx)
            *out_idx = i;
        return 0;
    }
    *out = val;
    if (out_idx)
        *out_idx = end - tmp + idx;
    return 1;
}

int looks_like_slider_comment(const char *str, int n, int idx,
                              slider_spec_t *out) { // see if 'idx' is inside a /*0======5*/<whitespace>number type comment.
    int i = idx;
    while (i >= 0 && str[i] != '/' && str[i] != '\n')
        i--;
    // ok it must be of the form /*digits-----digits*/
    if (str[i] != '/' || str[i + 1] != '*')
        return 0;
    out->start_idx = i + 1;
    i += 2; // skip /*
    try_parse_number(str, n, i, &out->minval, &i, 0.f);
    if (i >= n || str[i] != '=')
        return 0;
    while (i < n && str[i] == '=')
        i++;
    try_parse_number(str, n, i, &out->maxval, &i, 1.f);
    if (i >= n || str[i] != '*')
        return 0;
    else
        ++i;
    out->end_idx = i;
    if (i >= n || str[i] != '/')
        return 0;
    else
        ++i;
    while (i < n && isspace(str[i]))
        i++;
    out->value_start_idx = i;
    if (!try_parse_number(str, n, i, &out->curval, &i, 0.f))
        return 0;
    out->value_end_idx = i;
    return 1;
}

edit_op_t apply_edit_op(EditorState *E, edit_op_t op, int update_cursor_idx) {
    int old_cursor_idx = E->cursor_idx;
    int old_select_idx = E->select_idx;
    char *removed_str = NULL;
    if (op.remove_end > op.remove_start) {
        removed_str = stbstring_from_span(E->str + op.remove_start, E->str + op.remove_end, 0);
        stbds_arrdeln(E->str, op.remove_start, op.remove_end - op.remove_start);
        stbds_arrdeln(E->new_idx_to_old_idx, op.remove_start, op.remove_end - op.remove_start);
    }
    int numins = op.insert_str ? strlen(op.insert_str) : 0;
    if (numins) {
        stbds_arrinsn(E->str, op.remove_start, numins);
        stbds_arrinsn(E->new_idx_to_old_idx, op.remove_start, numins);
        memcpy(E->str + op.remove_start, op.insert_str, numins);
        for (int i = op.remove_start; i < op.remove_start + numins; i++) {
            E->new_idx_to_old_idx[i] = -1;
        }
    }
    if (update_cursor_idx) {
        if (E->cursor_idx >= op.remove_end)
            E->cursor_idx -= op.remove_end - op.remove_start;
        else if (E->cursor_idx >= op.remove_start)
            E->cursor_idx = op.remove_start;
        if (E->cursor_idx >= op.remove_start && op.insert_str)
            E->cursor_idx += strlen(op.insert_str);
        E->select_idx = E->cursor_idx;
    }
    // flip the op! remove_end is set to the insert string length, and the string is set to the characters we removed.
    op.remove_end = op.remove_start + numins;
    op.insert_str = removed_str;
    op.cursor_idx_after = old_cursor_idx;
    op.select_idx_after = old_select_idx;
    return op;
}

void push_edit_op(EditorState *E, int remove_start, int remove_end, const char *insert_str, int update_cursor_idx) {
    edit_op_t op = {min(remove_start, remove_end), max(remove_start, remove_end), (char *)insert_str};
    edit_op_t undo_op = apply_edit_op(E, op, update_cursor_idx);
    // delete any undo state after the current point...
    int num_undos = stbds_arrlen(E->edit_ops);
    for (int i = E->undo_idx; i < num_undos; i++) {
        stbds_arrfree(E->edit_ops[i].insert_str);
    }
    // ...and add this op as the last one in the stack. unless we can merge it
    // TODO undo merging
    stbds_arrsetlen(E->edit_ops, E->undo_idx + 1);
    E->edit_ops[E->undo_idx++] = undo_op;
}

void undo_redo(EditorState *E, int is_redo) {
    if (!is_redo && E->undo_idx <= 0)
        return;
    if (is_redo && E->undo_idx >= stbds_arrlen(E->edit_ops))
        return;
    if (!is_redo)
        E->undo_idx--;
    edit_op_t undo_op = E->edit_ops[E->undo_idx];
    edit_op_t redo_op = apply_edit_op(E, undo_op, 1);
    E->cursor_idx = undo_op.cursor_idx_after;
    E->select_idx = undo_op.select_idx_after;
    stbds_arrfree(undo_op.insert_str);
    E->edit_ops[E->undo_idx] = redo_op;
    if (is_redo)
        E->undo_idx++;
}

void undo(EditorState *E) { undo_redo(E, 0); }

void redo(EditorState *E) { undo_redo(E, 1); }

static inline int get_select_start(EditorState *obj) { return min(obj->cursor_idx, obj->select_idx); }
static inline int get_select_end(EditorState *obj) { return max(obj->cursor_idx, obj->select_idx); }

static inline int next_tab(int x) { return (x + 4) & ~3; }

int xy_to_idx_slow(EditorState *E, int tx, int ty) {
    tx = max(0, tx);
    ty = max(0, ty);
    int n = stbds_arrlen(E->character_pos);
    int rv = -1;
    for (int i = 0; i < n; i++) {
        if (E->character_pos[i].y == ty) {
            if (E->character_pos[i].x <= tx || rv < 0)
                rv = i;
        }
    }
    if (rv < 0)
        rv = n;
    return rv;
}

bool idx_to_xy(EditorState *E, int idx, int *tx, int *ty) {
    if (idx < 0 || idx >= stbds_arrlen(E->character_pos))
        return false;
    *tx = E->character_pos[idx].x;
    *ty = E->character_pos[idx].y;
    return true;
}

static void adjust_font_size(EditorState *E, int delta) {
    float xzoom = E->cursor_x;
    float yzoom = E->cursor_y;
    E->scroll_target_x = E->scroll_target_x / E->font_width - xzoom;
    E->scroll_target_y = E->scroll_target_y / E->font_height - yzoom;
    E->scroll_x = E->scroll_x / E->font_width - xzoom;
    E->scroll_y = E->scroll_y / E->font_height - yzoom;
    E->font_width = clamp(E->font_width + delta, 8, 256);
    E->font_height = E->font_width * 2;
    E->scroll_target_x = (E->scroll_target_x + xzoom) * E->font_width;
    E->scroll_target_y = (E->scroll_target_y + yzoom) * E->font_height;
    E->scroll_x = (E->scroll_x + xzoom) * E->font_width;
    E->scroll_y = (E->scroll_y + yzoom) * E->font_height;
}

int count_leading_spaces(EditorState *E, int start_idx) {
    int n = stbds_arrlen(E->str);
    int x = 0;
    for (int i = start_idx; i < n; i++) {
        if (E->str[i] == '\t')
            x = next_tab(x);
        else if (E->str[i] == ' ')
            x++;
        else
            break;
    }
    return x;
}

void find_word_at_idx(EditorState *E, int cursor_idx, int *start_idx, int *end_idx) {
    int idx, idx2;
    for (idx = cursor_idx; idx < stbds_arrlen(E->str); ++idx)
        if (iswordbreak(E->str[idx]))
            break;
    for (idx2 = cursor_idx - 1; idx2 >= 0; --idx2)
        if (iswordbreak(E->str[idx2]))
            break;
    *start_idx = idx2 + 1;
    *end_idx = idx;
}

void find_line_at_idx(EditorState *E, int cursor_idx, int *start_idx, int *end_idx) {
    int idx, idx2;
    for (idx = cursor_idx; idx < stbds_arrlen(E->str); ++idx)
        if (isnewline(E->str[idx]))
            break;
    for (idx2 = E->cursor_idx; idx2 >= 0; --idx2)
        if (isnewline(E->str[idx2]))
            break;
    *start_idx = idx2 + 1;
    *end_idx = idx;
}

static inline void postpone_autocomplete_show(EditorState *E) { E->autocomplete_show_after = G->iTime + 0.5f; }

void editor_click(EditorState *E, basic_state_t *G, float x, float y, int is_drag, int click_count) {
    postpone_autocomplete_show(E);
    x += E->scroll_x;
    y += E->scroll_y;
    int tmw = (fbw - 64.f) / E->font_width;
    float fx = (x / E->font_width + 0.5f);
    float fy = (y / E->font_height);
    int cx = (int)fx;
    int cy = (int)fy;
    if (!is_drag) {
        E->click_fx = fx;
        E->click_fy = fy;
        E->mouse_dragging_chart = E->mouse_hovering_chart;
    }
    if (E->mouse_hovering_chart && is_drag < 0) {
        E->mouse_clicked_chart = click_count > 0;
    }
    if (!E->mouse_dragging_chart) {
        // sliders on the right interaction

        if (E->click_fx >= tmw - 16 && E->click_fx < tmw) {
            for (int slideridx = 0; slideridx < 16; ++slideridx) {
                /*
                for (int i = 0; i < G->sliders[slideridx].n; i += 2) {
                    int line = G->sliders[slideridx].data[i + 1];
                    if (line == (int)E->click_fy) {
                        if (!is_drag) {
                            E->click_slider_value = G->sliders[slideridx].data[i];
                        } else {
                            float newvalue = clamp(E->click_slider_value + (fx - E->click_fx) / 16.f, 0.f, 1.f);
                            G->sliders[slideridx].data[i] = newvalue;
                        }
                        // printf("dragging slider on line %d\n", line);
                        return;
                    }
                }*/
            }
        }
        // text editor mouse interaction

        // int looks_like_slider_comment(const char *str, int n, int idx,slider_spec_t *out) { // see if 'idx' is inside a
        // /*0======5*/<whitespace>number type comment.
        int click_idx = xy_to_idx_slow(E, cx, cy);
        if (!is_drag) {
            E->click_down_idx = click_idx;
        }
        slider_spec_t slider_spec;
        if (looks_like_slider_comment(E->str, stbds_arrlen(E->str), E->click_down_idx, &slider_spec)) {
            int slider_x1, slider_x2, slider_y;
            if (idx_to_xy(E, slider_spec.start_idx, &slider_x1, &slider_y) &&
                idx_to_xy(E, slider_spec.end_idx, &slider_x2, &slider_y)) {
                if (!is_drag) {
                    E->click_slider_value = slider_spec.curval;
                }
                float dv = fx - E->click_fx;
                float v = E->click_slider_value + (slider_spec.maxval - slider_spec.minval) * (dv / (slider_x2 - slider_x1));
                v = clamp(v, slider_spec.minval, slider_spec.maxval);
                // printf("slider value: %f\n", v);
                if (v != slider_spec.curval) {
                    char buf[32];
                    int numdecimals = clamp(3.f - log10f(slider_spec.maxval - slider_spec.minval), 0.f, 5.f);
                    char fmtbuf[32];
                    snprintf(fmtbuf, sizeof(fmtbuf), "%%0.%df", numdecimals);
                    snprintf(buf, sizeof(buf), fmtbuf, v);
                    char *end = buf + strlen(buf);
                    if (strrchr(buf, '.'))
                        while (end > buf && end[-1] == '0')
                            --end;
                    *end = 0;
                    // TODO : undo merging. for now, just poke the text in directly.
                    stbds_arrdeln(E->str, slider_spec.value_start_idx, slider_spec.value_end_idx - slider_spec.value_start_idx);
                    stbds_arrdeln(E->new_idx_to_old_idx, slider_spec.value_start_idx,
                                  slider_spec.value_end_idx - slider_spec.value_start_idx);
                    stbds_arrinsn(E->str, slider_spec.value_start_idx, strlen(buf));
                    memcpy(E->str + slider_spec.value_start_idx, buf, strlen(buf));
                    stbds_arrinsn(E->new_idx_to_old_idx, slider_spec.value_start_idx, strlen(buf));
                    for (int i = slider_spec.value_start_idx; i < slider_spec.value_start_idx + strlen(buf); i++) {
                        E->new_idx_to_old_idx[i] = -1;
                    }
                }
            }
        } else {
            E->cursor_idx = click_idx;
            if (!is_drag)
                E->select_idx = E->cursor_idx;
            idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
            E->cursor_x_target = E->cursor_x;
            if (is_drag < 0 && click_count == 2) {
                // double click - select word
                find_word_at_idx(E, E->cursor_idx, &E->select_idx, &E->cursor_idx);
            }
            if (is_drag < 0 && click_count == 3) {
                // triple click - select line
                find_line_at_idx(E, E->cursor_idx, &E->select_idx, &E->cursor_idx);
            }
        }
    }
}

static inline bool isspaceortab(char c) { return c == ' ' || c == '\t'; }

static inline int find_start_of_line(EditorState *E, int idx) {
    while (idx > 0 && E->str[idx - 1] != '\n')
        idx--;
    return idx;
}

static inline int find_end_of_line(EditorState *E, int idx) {
    while (idx < stbds_arrlen(E->str) && E->str[idx] != '\n')
        idx++;
    return idx;
}

int jump_to_found_text(EditorState *E, int backwards, int extra_char) {
    int delta = backwards ? -1 : 1;
    int ss = get_select_start(E);
    int se = get_select_end(E);
    const char *needle = E->str + ss;
    int needle_len = se - ss;
    if (!extra_char) {
        ss += delta;
        se += delta;
    }
    int n = stbds_arrlen(E->str);
    if (extra_char)
        n--;
    while (ss >= 0 && se <= n) {
        if (strncmp(needle, E->str + ss, needle_len) == 0 && (extra_char == 0 || E->str[ss + needle_len] == extra_char)) {
            E->select_idx = ss;
            E->cursor_idx = ss + needle_len + (extra_char ? 1 : 0);
            return 1;
        }
        ss += delta;
        se += delta;
    }
    return 0;
}

void editor_key(GLFWwindow *win, EditorState *E, int key) {
    int n = stbds_arrlen(E->str);
    int mods = key >> 16;
    int shift = mods & GLFW_MOD_SHIFT;
    int ctrl = mods & GLFW_MOD_CONTROL;
    int super = mods & GLFW_MOD_SUPER;
    int has_selection = E->cursor_idx != E->select_idx;
    int set_target_x = 1;
    int reset_selection = 0;
    idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
    if (key == GLFW_KEY_ESCAPE && mods == 0) {
        if (E->find_mode) {
            E->find_mode = false;
        } else if (E->autocomplete_options) {
            E->autocomplete_options = NULL;
            E->autocomplete_index = 0;
            E->autocomplete_scroll_y = 0;
            E->autocomplete_show_after = G->iTime + 10000.f; // completely ban autocomplete if you press escape.
        } else {
            E->select_idx = E->cursor_idx;
        }
    }

    if (super)
        switch (key & 0xFFFF) {
        case GLFW_KEY_UP:
        case GLFW_KEY_DOWN: {
            int word_start, word_end;
            int n = stbds_arrlen(E->str);
            find_word_at_idx(E, E->cursor_idx, &word_start, &word_end);
            float number = 0.f;
            int number_idx = 0;
            const char *end = E->str + word_end;
            int midinote = parse_midinote(E->str + word_start, E->str + word_end, &end, 0);
            if (midinote >= 0) {
                midinote += (key & 0xffff) == GLFW_KEY_UP ? 1 : -1;
                midinote = clamp(midinote, 0, 127);
                const char *buf = print_midinote(midinote);
                push_edit_op(E, word_start, word_end, buf, 0);
                return;
            } else if (try_parse_number(E->str, n, word_start, &number, &number_idx, 0.f)) {
                while (word_end > word_start && !ispartofnumber(E->str[word_end - 1]))
                    word_end--;
                // count decimal places
                int dp = -1;
                for (int i = word_start; i < word_end; i++)
                    if (E->str[i] == '.')
                        dp = i;
                int numdp = (dp < 0) ? 0 : max(1, word_end - dp - 1);
                number += powf(10.f, -numdp) * ((key & 0xffff) == GLFW_KEY_UP ? 1.f : -1.f);
                char buf[32];
                char fmt[32];
                snprintf(fmt, sizeof(fmt), "%%0.%df", numdp);
                snprintf(buf, sizeof(buf), fmt, number);
                push_edit_op(E, word_start, word_end, buf, 0);
                return;
            }
            break;
        }
        case GLFW_KEY_MINUS:
            adjust_font_size(E, -1);
            break;
        case GLFW_KEY_EQUAL:
            adjust_font_size(E, 1);
            break;
        case GLFW_KEY_Z:
            if (!shift)
                undo(E);
            else
                redo(E);
            break;
        case GLFW_KEY_Y:
            redo(E);
            break;
        case GLFW_KEY_A:
            E->cursor_idx = n;
            E->select_idx = 0;
            break;
        case GLFW_KEY_F: {
            E->find_mode = true;
            break;
        } break;
        case GLFW_KEY_C:
        case GLFW_KEY_X: {
            int se = get_select_start(E);
            int ee = get_select_end(E);
            char *str = stbstring_from_span(E->str + se, E->str + ee, 0);
            if (str) {
                glfwSetClipboardString(win, str);
                stbds_arrfree(str);
                if ((key & 0xffff) == GLFW_KEY_X) {
                    push_edit_op(E, se, ee, NULL, 1);
                    E->find_mode = false;
                }
            }
            break;
        }
        case GLFW_KEY_V: {
            const char *str = glfwGetClipboardString(win);
            if (str) {
                push_edit_op(E, get_select_start(E), get_select_end(E), str, 1);
                E->find_mode = false;
            }
            break;
        }
        case GLFW_KEY_SLASH: {
            {
                int ss = get_select_start(E);
                int se = get_select_end(E);
                int sx, sy, ex, ey;
                if (idx_to_xy(E, ss, &sx, &sy) && idx_to_xy(E, se, &ex, &ey)) {
                    ss = find_start_of_line(E, ss);
                    se = find_end_of_line(E, se);
                    char *str = stbstring_from_span(E->str + ss, E->str + se, ((ey - sy) + 1) * 4);
                    int minx = 0x7fffffff;
                    int all_commented = true;
                    int n = se - ss;
                    for (int idx = 0; idx < n;) {
                        int x = 0;
                        while (idx < n && isspaceortab(str[idx])) {
                            if (str[idx] == '\t')
                                x = next_tab(x);
                            else
                                ++x;
                            ++idx;
                        }
                        if (idx < n && str[idx] == '\n') {
                            ++idx;
                            continue;
                        } // skip blank lines
                        minx = min(x, minx);
                        if (idx + 2 >= n || str[idx] != '/' || str[idx + 1] != '/')
                            all_commented = false;
                        while (idx < n) {
                            ++idx;
                            if (str[idx - 1] == '\n')
                                break;
                        }
                    }
                    if (all_commented) { // remove the comments!
                        int dstidx = 0;
                        for (int idx = 0; idx < n;) {
                            while (idx < n && isspaceortab(str[idx]))
                                str[dstidx++] = str[idx++];
                            if (idx + 2 < n && str[idx] == '/' && str[idx + 1] == '/') {
                                int oldidx = idx;
                                idx += 2;
                                if (idx < n && isspaceortab(str[idx]))
                                    idx++;
                                if (idx < E->cursor_idx)
                                    E->cursor_idx += oldidx - idx;
                                if (idx < E->select_idx)
                                    E->select_idx += oldidx - idx;
                            }
                            while (idx < n) {
                                str[dstidx++] = str[idx++];
                                if (str[dstidx - 1] == '\n')
                                    break;
                            }
                        }
                        str[dstidx] = 0;
                    } else { // add comments at minx
                        for (int idx = 0; idx < n;) {
                            int x = 0;
                            while (idx < n && x < minx && isspaceortab(str[idx])) {
                                if (str[idx] == '\t')
                                    x = next_tab(x);
                                else
                                    ++x;
                                ++idx;
                            }
                            if (idx < n && str[idx] != '\n') {
                                memmove(str + idx + 3, str + idx, n - idx);
                                str[idx++] = '/';
                                str[idx++] = '/';
                                str[idx++] = ' ';
                                n += 3;
                                if (idx <= E->cursor_idx)
                                    E->cursor_idx += 3;
                                if (idx <= E->select_idx)
                                    E->select_idx += 3;
                            }
                            while (idx < n) {
                                ++idx;
                                if (str[idx - 1] == '\n')
                                    break;
                            }
                        }
                        str[n] = 0;
                    }
                    push_edit_op(E, ss, se, str, 0);
                    E->find_mode = false;
                    if (sy != ey) {
                        E->select_idx = ss;
                        E->cursor_idx = ss + strlen(str);
                    }
                    stbds_arrfree(str);
                    break;
                }
                break;
            }
        }
        }
    bool autocomplete_valid = !shift && E->autocomplete_options && E->autocomplete_index >= 0 &&
                              E->autocomplete_index < stbds_hmlen(E->autocomplete_options);
    if (autocomplete_valid && (key == '\t' || key == '\n')) {
        autocomplete_option_t *option = &E->autocomplete_options[E->autocomplete_index];
        int matchfrom = E->cursor_idx - option->matchlen;
        if (matchfrom < 0)
            matchfrom = 0;
        push_edit_op(E, matchfrom, matchfrom + option->matchlen, option->key, 1);
    } else
        switch (key & 0xFFFF) {
        case '\b':
        case GLFW_KEY_BACKSPACE:
            if (E->find_mode) {
                int ss = get_select_start(E);
                int se = get_select_end(E);
                E->select_idx = ss;
                E->cursor_idx = max(ss, se - 1);
                break;
            } else if (has_selection) {
                push_edit_op(E, get_select_start(E), get_select_end(E), NULL, 1);
            } else if (E->cursor_idx > 0) {
                push_edit_op(E, E->cursor_idx - 1, E->cursor_idx, NULL, 1);
            }
            break;
        case GLFW_KEY_DELETE:
            if (!E->find_mode) {
                push_edit_op(E, E->cursor_idx, E->cursor_idx + 1, NULL, 1);
            }
            break;
        // TODO: tab/shift-tab should indent or unindent the whole selection; unless its tab after leading space, in which case its
        // just an insert.
        case '\t': {
            if (E->find_mode) {
                jump_to_found_text(E, shift, 0);
            } else {
                int ss = get_select_start(E);
                int se = get_select_end(E);
                int sx, sy, ex, ey;
                if (!idx_to_xy(E, ss, &sx, &sy) || !idx_to_xy(E, se, &ex, &ey))
                    goto insert_character;
                if (sy == ey && !shift)
                    goto insert_character;
                ss = find_start_of_line(E, ss);
                se = find_end_of_line(E, se);
                char *str = stbstring_from_span(E->str + ss, E->str + se, ((ey - sy) + 1) * 4);
                for (char *c = str; *c;) {
                    if (shift) {
                        // skip up to 4 spaces or a tab.
                        char *nextchar = c;
                        for (int i = 0; i < 4; ++i, ++nextchar)
                            if (*nextchar == '\t') {
                                nextchar++;
                                break;
                            } else if (*nextchar != ' ')
                                break;
                        memmove(c, nextchar, strlen(nextchar) + 1);
                    } else {
                        memmove(c + 4, c, strlen(c) + 1);
                        memset(c, ' ', 4);
                    }
                    while (*c && *c != '\n')
                        ++c;
                    if (*c == '\n')
                        ++c;
                }
                push_edit_op(E, ss, se, str, 1);
                E->select_idx = ss;
                E->cursor_idx = ss + strlen(str);
                stbds_arrfree(str);
            }
            break;
        }
        case '\n':

        case ' ' ... '~':
        insert_character:
            if (!super && !ctrl) {
                if (E->find_mode) {
                    jump_to_found_text(E, shift, key);
                } else {
                    E->autocomplete_show_after = 0.f; // after typing, we can immediately show autocomplete.
                    // delete the selection; insert the character
                    int ls = (key == '\n') ? count_leading_spaces(E, find_start_of_line(E, E->cursor_idx)) : 0;
                    char buf[ls + 2];
                    buf[0] = key;
                    memset(buf + 1, ' ', ls);
                    buf[ls + 1] = 0;
                    push_edit_op(E, get_select_start(E), get_select_end(E), buf, 1);
                }
            }
            break;
        case GLFW_KEY_LEFT:
            postpone_autocomplete_show(E);
            if (super) {
                // same as home
                int ls = count_leading_spaces(E, find_start_of_line(E, E->cursor_idx));
                E->cursor_idx = xy_to_idx_slow(E, (E->cursor_x > ls) ? ls : 0, E->cursor_y);
            } else if (has_selection && !shift) {
                E->cursor_idx = get_select_start(E);
            } else
                E->cursor_idx = max(0, E->cursor_idx - 1);
            reset_selection = !shift;
            break;
        case GLFW_KEY_RIGHT:
            postpone_autocomplete_show(E);
            if (super) {
                // same as end
                E->cursor_idx = find_end_of_line(E, E->cursor_idx);
            } else if (has_selection && !shift) {
                E->cursor_idx = get_select_end(E);
            } else
                E->cursor_idx = min(E->cursor_idx + 1, n);
            reset_selection = !shift;
            break;

        case GLFW_KEY_UP:
            if (E->find_mode) {
                jump_to_found_text(E, 1, 0);
                postpone_autocomplete_show(E);
            } else if (E->autocomplete_options) {
                E->autocomplete_scroll_y--;
            } else {
                postpone_autocomplete_show(E);
                E->cursor_idx = super ? 0 : xy_to_idx_slow(E, E->cursor_x_target, E->cursor_y - 1);
                set_target_x = 0;
                reset_selection = !shift;
            }
            break;
        case GLFW_KEY_DOWN:
            if (E->find_mode) {
                jump_to_found_text(E, 0, 0);
                postpone_autocomplete_show(E);
            } else if (E->autocomplete_options) {
                E->autocomplete_scroll_y++;
            } else {
                postpone_autocomplete_show(E);
                E->cursor_idx = super ? n : xy_to_idx_slow(E, E->cursor_x_target, E->cursor_y + 1);
                set_target_x = 0;
                reset_selection = !shift;
            }
            break;
        case GLFW_KEY_HOME: {
            postpone_autocomplete_show(E);
            int ls = count_leading_spaces(E, find_start_of_line(E, E->cursor_idx));
            E->cursor_idx = xy_to_idx_slow(E, (E->cursor_x > ls) ? ls : 0, E->cursor_y);
            reset_selection = !shift;
            break;
        }
        case GLFW_KEY_END:
            postpone_autocomplete_show(E);
            E->cursor_idx = find_end_of_line(E, E->cursor_idx);
            reset_selection = !shift;
            break;
        case GLFW_KEY_PAGE_UP:
            postpone_autocomplete_show(E);
            if (E->find_mode) {
                jump_to_found_text(E, 1, 0);
            } else {
                E->cursor_idx = super ? 0 : xy_to_idx_slow(E, E->cursor_x_target, E->cursor_y - 20);
                set_target_x = 0;
                reset_selection = !shift;
            }
            break;
        case GLFW_KEY_PAGE_DOWN:
            postpone_autocomplete_show(E);
            if (E->find_mode) {
                jump_to_found_text(E, 0, 0);
            } else {
                E->cursor_idx = super ? n : xy_to_idx_slow(E, E->cursor_x_target, E->cursor_y + 20);
                set_target_x = 0;
                reset_selection = !shift;
            }
            break;
        }
    if (reset_selection) {
        E->select_idx = E->cursor_idx;
        E->find_mode = 0;
    }
    idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
    if (set_target_x)
        E->cursor_x_target = E->cursor_x;
}

// --- colors 3 digits bg, 3 digits fg, 2 digits character.
#define C_SELECTION 0xc48fff00u
#define C_SELECTION_FIND_MODE 0x4c400000u

#define C_PATTERN_LINE 0x80444444u   // this one is 24 bit RGBA, unlike the others :( (add_line uses it)
#define C_PATTERN_GUTTER 0x80444444u // same
#define C_PATTERN_JUMP_MARKER 0x000f0000u

#define C_PATTERN_BG 0x11100000u
#define C_TRACKER_BG 0x12300000u
#define C_TRACKER_BG_4 0x22400000u
#define C_TRACKER_BG_8 0x33500000u
#define C_TRACKER_BG_HILITE 0x44600000u
#define C_HILITE_NOTE 0xe4900000u

#define C_AUTOCOMPLETE_SECONDARY 0x111c4800u
#define C_AUTOCOMPLETE 0xc4800000u

#define C_NUM 0x000cc700u
#define C_DEF 0x000fff00u
#define C_KW 0x0009ac00u
#define C_TYPE 0x000a9c00u
#define C_PREPROC 0x0009a900u
#define C_SOUND 0x000e8400u
#define C_STR 0x0008df00u
#define C_COM 0x000fd800u
#define C_PUN 0x000aaa00u
#define C_ERR 0xf00fff00u
#define C_OK 0x080fff00u
#define C_WARNING 0xfa400000u
#define C_CHART 0x1312a400u // min notation chart
#define C_CHART_HOVER 0x2422a400u
#define C_CHART_DRAG C_CHART_HOVER
#define C_CHART_HILITE 0x4643b500u
#define C_NOTE 0x0000f400u

#define C_SLIDER 0x11148f00u
#define C_SLIDER_RED 0x111e4300u
#define C_SLIDER_RED2 0x111e7400u
#define C_SLIDER_ORANGE 0x111fa400u
#define C_SLIDER_ORANGE2 0x111fc300u
#define C_SLIDER_YELLOW 0x111ee200u
#define C_SLIDER_YELLOW2 0x111efa00u
#define C_SLIDER_WHITE 0x111eff00u
#define C_SLIDER_WHITE2 0x1118cf00u

static const uint32_t slidercols[17] = {C_SLIDER_RED,     C_SLIDER_RED2,    C_SLIDER_ORANGE, C_SLIDER_ORANGE2, C_SLIDER_YELLOW,
                                        C_SLIDER_YELLOW2, C_SLIDER_WHITE,   C_SLIDER_WHITE2, C_SLIDER_RED,     C_SLIDER_RED2,
                                        C_SLIDER_ORANGE,  C_SLIDER_ORANGE2, C_SLIDER_YELLOW, C_SLIDER_YELLOW2, C_SLIDER_WHITE,
                                        C_SLIDER_WHITE2,  C_SLIDER};

uint32_t invert_color(uint32_t col) { // swap fg and bg
    return ((col >> 12) & 0xfff00) + ((col & 0xfff00) << 12) + (col & 0xff);
}

char status_bar[512];
double status_bar_time = 0;
uint32_t status_bar_color = 0;

void set_status_bar(uint32_t color, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vsnprintf(status_bar, sizeof(status_bar) - 1, msg, args);
    va_end(args);
    // fprintf(stderr, "%s\n", status_bar);
    status_bar_color = color;
    status_bar_time = glfwGetTime();
}

float *closest_slider[16] = {}; // 16 closest sliders to the cursor, for midi cc

void parse_error_log(EditorState *E) {
    hmfree(E->error_msgs);
    E->error_msgs = NULL;
    int fnamelen = strlen(E->fname);
    for (const char *c = E->last_compile_log; *c; c++) {
        int col = 0, line = 0;
        if (sscanf(c, "ERROR: %d:%d:", &col, &line) == 2) {
            int val = line - 1;
            hmput(E->error_msgs, val, c);
        }
        if (c[0] == '.' && c[1] == '/')
            c += 2;
        if (strncmp(c, E->fname, fnamelen) == 0 && c[fnamelen] == ':') {
            const char *colend = c + fnamelen + 1;
            int line = strtol(colend, (char **)&colend, 10);
            if (line) {
                while (*colend && !isspace(*colend))
                    ++colend;
                while (isspace(*colend))
                    ++colend;
                // printf("compile error on line %d, column %d, [%s]\n", line, col, colend);
                int val = line - 1;
                hmput(E->error_msgs, val, colend);
            }
        }
        while (*c && *c != '\n')
            c++;
    }
}

static inline int is_ident_start(unsigned c) { return (c == '_') || ((c | 32) - 'a' < 26); }
static inline int is_ident_cont(unsigned c) { return is_ident_start(c) || (c - '0' < 10); }
static inline int is_space(unsigned c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

static inline int unary_sign_context(unsigned prev) {
    return !(is_ident_cont(prev) || prev == ')' || prev == ']' || prev == '}' || prev == '.');
}

// ------- number scanner (C & GLSL friendly, incl. -0.53e-04f, 0x1.fp+2f) -------
// returns index AFTER the number starting at i. if not a number, returns i.
static inline int scan_number(const char *s, int i, int n, unsigned prev_nonspace) {
    int j = i;

    // optional sign (only if it looks unary)
    if ((s[j] == '+' || s[j] == '-') && j + 1 < n && ((s[j + 1] >= '0' && s[j + 1] <= '9') || s[j + 1] == '.') &&
        unary_sign_context(prev_nonspace))
        ++j;

    // leading dot form: .123 or .123e-2
    if (j < n && s[j] == '.' && j + 1 < n && (unsigned)(s[j + 1] - '0') < 10) {
        ++j;
        while (j < n && (unsigned)(s[j] - '0') < 10)
            ++j;
        if (j < n && (s[j] == 'e' || s[j] == 'E')) { // exponent
            int k = j + 1;
            if (k < n && (s[k] == '+' || s[k] == '-'))
                ++k;
            int d = k;
            while (d < n && (unsigned)(s[d] - '0') < 10)
                ++d;
            if (d > k)
                j = d;
        }
        while (j < n && (s[j] == 'f' || s[j] == 'F'))
            ++j; // GLSL/C float suffix
        return j;
    }

    // hex (0x...): integer or hex-float 0x1.fp+2
    if (j + 1 < n && s[j] == '0' && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
        int had = 0;
        while (j < n && ((unsigned)(s[j] - '0') < 10 || (unsigned)((s[j] | 32) - 'a') < 6)) {
            ++j;
            had = 1;
        }
        if (j < n && s[j] == '.') {
            ++j;
            while (j < n && ((unsigned)(s[j] - '0') < 10 || (unsigned)((s[j] | 32) - 'a') < 6))
                ++j;
            had = 1;
        }
        if (had && j < n && (s[j] == 'p' || s[j] == 'P')) { // hex float exp
            int k = j + 1;
            if (k < n && (s[k] == '+' || s[k] == '-'))
                ++k;
            int d = k;
            while (d < n && (unsigned)(s[d] - '0') < 10)
                ++d;
            if (d > k)
                j = d;
        }
        // suffixes (C): uUlLfF (accept generously; GLSL mostly ignores)
        while (j < n && (s[j] == 'u' || s[j] == 'U' || s[j] == 'l' || s[j] == 'L' || s[j] == 'f' || s[j] == 'F'))
            ++j;
        return j;
    }

    // decimal int/float
    int start = j, saw_digit = 0;
    while (j < n && (unsigned)(s[j] - '0') < 10) {
        ++j;
        saw_digit = 1;
    }
    if (j < n && s[j] == '.') {
        ++j;
        while (j < n && (unsigned)(s[j] - '0') < 10) {
            ++j;
            saw_digit = 1;
        }
    }
    if (saw_digit && j < n && (s[j] == 'e' || s[j] == 'E')) {
        int k = j + 1;
        if (k < n && (s[k] == '+' || s[k] == '-'))
            ++k;
        int d = k;
        while (d < n && (unsigned)(s[d] - '0') < 10)
            ++d;
        if (d > k)
            j = d;
    }
    if (j > start) {
        while (j < n && (s[j] == 'u' || s[j] == 'U' || s[j] == 'l' || s[j] == 'L' || s[j] == 'f' || s[j] == 'F'))
            ++j; // suffixes
        return j;
    }
    return i;
}

// scan C/GLSL identifier
static inline int scan_ident(const char *s, int i, int n) {
    if (!is_ident_start((unsigned)s[i]))
        return i;
    int j = i + 1;
    while (j < n && is_ident_cont((unsigned)s[j]))
        ++j;
    return j;
}

// string and char literals (with escapes)
static inline int scan_string(const char *s, int i, int n, char q) {
    int j = i + 1;
    while (j < n) {
        char c = s[j++];
        if (c == '\\' && j < n) {
            ++j;
            continue;
        }
        if (c == q)
            break;
        if (c == '\n')
            break; // unterminated -> color till EOL
    }
    return j;
}

// comments
static inline int scan_line_comment(const char *s, int i, int n) {
    int j = i + 2;
    while (j < n && s[j] != '\n')
        ++j;
    return j;
}
static inline int scan_block_comment(const char *s, int i, int n) {
    int j = i + 2;
    while (j + 1 < n) {
        if (s[j] == '*' && s[j + 1] == '/') {
            j += 2;
            break;
        }
        ++j;
    }
    return j;
}

#define MAX_BRACK 256
typedef struct tokenizer_t {
    int n;           // num chars
    const char *str; // source
    char stk[MAX_BRACK];
    int stk_x[MAX_BRACK];
    int stk_y[MAX_BRACK];
    int sp, paren, brack, brace;
    int x, y;               // write position
    int cursor_x, cursor_y; // cursor position
    uint32_t *ptr;          // destination
    char prev_nonspace;     // previous non-space char
} tokenizer_t;

char tok_get(tokenizer_t *t, int i) { return (i < t->n && i >= 0) ? t->str[i] : 0; }

int push_bracket(tokenizer_t *t, int *count, char opc) {
    if (t->sp < MAX_BRACK) {
        t->stk_x[t->sp] = t->x;
        t->stk_y[t->sp] = t->y;
        t->stk[t->sp++] = opc;
    }
    (*count)++;
    if (t->x == t->cursor_x - 1 && t->y == t->cursor_y) {
        // over this opening bracket.
        return invert_color(C_PUN);
    }
    return C_PUN;
}

int close_bracket(tokenizer_t *t, int *count, char opc) {
    if (t->sp > 0 && t->stk[t->sp - 1] == opc) {
        t->sp--;
        (*count)++;
        int ox = t->stk_x[t->sp], oy = t->stk_y[t->sp];
        if (t->x == t->cursor_x - 1 && t->y == t->cursor_y) {
            // over this closing bracket. hilight the opening bracket
            if (oy >= 0 && oy < TMH && ox >= 0 && ox < TMW) {
                uint32_t char_and_col = t->ptr[oy * TMW + ox];
                t->ptr[oy * TMW + ox] = invert_color(char_and_col);
                return invert_color(C_PUN);
            }
        }
        if (ox == t->cursor_x - 1 && oy == t->cursor_y) {
            // over the opening bracket. hilight this closing bracket
            return invert_color(C_PUN);
        }
        return C_PUN;
    }
    return C_ERR;
}

void print_span_to_screen(uint32_t *ptr, int x, int y, uint32_t color, bool multi_line, const char *s, const char *e) {
    int leftx = x;
    if (y >= TMH)
        return;
    for (const char *c = s; c < e; c++) {
        if (*c == '\n') {
            if (!multi_line)
                break;
            x = leftx;
            y++;
            if (y >= TMH)
                return;
            continue;
        }
        if (y >= 0 && x < TMW && x >= 0) {
            ptr[y * TMW + x] = (color) | (unsigned char)(*c);
        }
        x++;
    }
}
int print_to_screen(uint32_t *ptr, int x, int y, uint32_t color, bool multi_line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(NULL, 0, fmt, args);
    if (length > TMW * 16)
        length = TMW * 16;
    va_end(args);
    va_start(args, fmt);
    char buf[length + 1];
    vsnprintf(buf, length + 1, fmt, args);
    va_end(args);
    print_span_to_screen(ptr, x, y, color, multi_line, buf, buf + length);
    return length;
}

int vertical_bar(int y) { // y=0 (empty) to y=16 (full)
    if (y <= 0)
        return ' ';
    return 128 + clamp(y - 1, 0, 15);
}

int horizontal_tick(int x) { // x=0 (4 pixel tick starting from left edge) to x=8 (off the right edge)
    if (x >= 8 || x < -4)
        return 128 + 16;
    return 128 + 16 + clamp(x + 4, 0, 15);
}

const char *find_line_end_or_comment(const char *s, const char *e) {
    while (s < e) {
        if (*s == '\n')
            return s;
        if (*s == '/' && s + 1 < e && s[1] == '/')
            return s;
        if (*s == '/' && s + 1 < e && s[1] == '*') {
            const char *c = s;
            while (1) {
                if (s + 2 > e || *s == '\n')
                    return c; // unterminated block comment
                if (s[0] == '*' && s[1] == '/') {
                    s += 2;
                    break;
                } // terminated block comment - keep going
            }
        }
        ++s;
    }
    return e;
}

static inline Sound *get_sound_span(const char *s, const char *e) { return get_sound(temp_cstring_from_span(s, e)); }

autocomplete_option_t autocomplete_score(const char *users, int userlen, int minmatch, const char *option) {
    autocomplete_option_t rv = {option};
    // find the longest substring of option that starts at 'users'
    int option_len = strlen(option);
    const char *option_end = option + option_len;
    for (const char *c = option; *c; c++) {
        int i;
        for (i = 0; i < userlen; i++)
            if (tolower(users[i]) != tolower(c[i]))
                break;
        if (c == option && c[i] == 0) // complete match doesnt count as 'completion'...
            continue;
        if (i < minmatch)
            continue;
        int score = i * 2 + 20 - (option_end - c) * 2;

        if (c == option)
            score += 20; // boost if the match is at the start
        else if (isupper(c[0]) && !isupper(c[-1]))
            score += 10;
        if (score > rv.value) {
            rv.value = score;
            rv.xoffset = c - option;
            rv.matchlen = i;
        }
    }
    return rv;
}

typedef struct hilite_region_t {
    int start, end;
    int color;
    int alpha;
} hilite_region_t;

int compare_hilite_regions(const void *a, const void *b) {
    const hilite_region_t *h1 = (const hilite_region_t *)a;
    const hilite_region_t *h2 = (const hilite_region_t *)b;
    return h1->end - h2->end;
}

uint32_t interp_color(uint32_t col1, uint32_t col2, int alpha) {
    int s = 256 - alpha;
    int t = alpha;
    return ((((((col1 >> 8) & 0xf) * s) + (((col2 >> 8) & 0xf) * t)) >> 8) << 8) |
           ((((((col1 >> 12) & 0xf) * s) + (((col2 >> 12) & 0xf) * t)) >> 8) << 12) |
           ((((((col1 >> 16) & 0xf) * s) + (((col2 >> 16) & 0xf) * t)) >> 8) << 16) |
           ((((((col1 >> 20) & 0xf) * s) + (((col2 >> 20) & 0xf) * t)) >> 8) << 20) |
           ((((((col1 >> 24) & 0xf) * s) + (((col2 >> 24) & 0xf) * t)) >> 8) << 24) |
           ((((((col1 >> 28) & 0xf) * s) + (((col2 >> 28) & 0xf) * t)) >> 8) << 28);
}

void clear_rectangle(uint32_t *ptr, int x1, int y1, int x2, int y2, uint32_t col) {
    x1 = clamp(x1, 0, TMW);
    x2 = clamp(x2, 0, TMW);
    y1 = clamp(y1, 0, TMH);
    y2 = clamp(y2, 0, TMH);
    for (int y = y1; y < y2; ++y) {
        for (int x = x1; x < x2; ++x) {
            ptr[y * TMW + x] = col;
        }
    }
}

int code_color(EditorState *E, uint32_t *ptr) {
    int left = 80 / E->font_width - E->intscroll_x;
    int first_left = left;
    tokenizer_t t = {.ptr = ptr,
                     .str = E->str,
                     .n = (int)stbds_arrlen(E->str),
                     .x = left,
                     .y = -E->intscroll_y,
                     .cursor_x = E->cursor_x - E->intscroll_x,
                     .cursor_y = E->cursor_y - E->intscroll_y,
                     .prev_nonspace = ';'};
    bool wasinclude = false;
    int se = get_select_start(E);
    int ee = get_select_end(E);
    float *sliders[TMH] = {};
    int sliderindices[TMH];
    float *new_closest_slider[16] = {};
    /*
    for (int slideridx = 0; slideridx < 16; ++slideridx) {
        for (int i = 0; i < G->sliders_hwm[slideridx]; i += 2) {
            float *value_line = G->sliders[slideridx].data + i;
            float value = value_line[0];
            int line = (int)value_line[1] - 1;
            int dist_to_old =
                new_closest_slider[slideridx] == NULL ? 1000000 : abs((int)new_closest_slider[slideridx][1] - E->cursor_y);
            int dist_to_new = abs(line - E->cursor_y);
            if (dist_to_old > dist_to_new) {
                new_closest_slider[slideridx] = value_line;
            }
            line -= E->intscroll;
            if (line >= 0 && line < TMH) {
                sliderindices[line] = slideridx;
                sliders[line] = value_line;
            }
        }
    }*/
    memcpy(closest_slider, new_closest_slider, sizeof(closest_slider));
    int tmw = (fbw - 64.f) / E->font_width;
    E->cursor_in_pattern_area = false;
    E->max_width = 0;
    int column_max_width = 0;
    int *column_widths = NULL;
    int cursor_in_type = 0;
    int cursor_token_start_idx = 0;
    int cursor_token_end_idx = 0;
    int pattern_entry_idx = -1;
    int pattern_mode = 0; // if >0, we are parsing pattern not C; we code colour differently, and only exit when we leave.
#define INVALID_LINE 0x7fffffff
    int first_grid_line_start = INVALID_LINE;
    int grid_line_end = INVALID_LINE;
    int grid_line_start = INVALID_LINE; // when we enter a grid (#), we start counting lines...
    int grid_line_hilight = -1;
    uint32_t empty_bgcol = 0x11100000u;
    pattern_t *cur_pattern = NULL;
    hilite_region_t *hilites = NULL;
    // hap_t viz_hap_mem[128];
    // hap_span_t viz_haps = {};
    int current_hilite_region = 0;
    slider_spec_t slider_spec;
    stbds_arrsetlen(E->character_pos, t.n);
    memset(E->character_pos, 0, sizeof(character_pos_t) * t.n);
#define jump_after_columns()                                                                                                       \
    if (stbds_arrlen(column_widths) > 0) {                                                                                         \
        int right = column_max_width + 2 - E->intscroll_x;                                                                         \
        add_line(E, first_left, first_grid_line_start, right, first_grid_line_start, C_PATTERN_LINE, 5.f);                         \
        add_line(E, left, t.y, right, t.y, C_PATTERN_LINE, 5.f);                                                                   \
        if (grid_line_end == INVALID_LINE || t.y > grid_line_end)                                                                  \
            grid_line_end = t.y;                                                                                                   \
        int jumpy = (grid_line_end == INVALID_LINE ? t.y : grid_line_end);                                                         \
        stbds_arrpush(column_widths, right);                                                                                       \
        stbds_arrpush(column_widths, t.y);                                                                                         \
        int nc = stbds_arrlen(column_widths);                                                                                      \
        for (int c = 0; c < nc; c += 2) {                                                                                          \
            clear_rectangle(t.ptr, (!c) ? first_left : column_widths[c - 2], column_widths[c + 1], column_widths[c], jumpy,        \
                            C_PATTERN_BG + ' ');                                                                                   \
            if (c < nc - 2)                                                                                                        \
                add_line(E, column_widths[c], first_grid_line_start, column_widths[c],                                             \
                         max(column_widths[c + 1], column_widths[c + 3]), C_PATTERN_GUTTER, 5.f);                                  \
        }                                                                                                                          \
        clear_rectangle(t.ptr, right, first_grid_line_start, tmw, jumpy, C_PATTERN_BG + ' ');                                       \
        t.y = jumpy;                                                                                                               \
        t.x = left = first_left;                                                                                                   \
        column_max_width = 0;                                                                                                      \
        grid_line_start = INVALID_LINE;                                                                                            \
        grid_line_end = INVALID_LINE;                                                                                              \
        first_grid_line_start = INVALID_LINE;                                                                                      \
    }

    for (int i = 0; i <= t.n;) {
        unsigned h = 0;
        char c = tok_get(&t, i);
        uint32_t col = C_DEF;
        int j = i + 1;
        bool token_is_curve = false;
        bool token_is_slider = false;
        switch (c) {
        case '(':
            col = push_bracket(&t, &t.paren, '(');
            break;
        case '[':
            col = push_bracket(&t, &t.brack, '[');
            break;
        case '{':
            col = push_bracket(&t, &t.brace, '{');
            break;
        case ')':
            col = close_bracket(&t, &t.paren, '(');
            break;
        case ']':
            col = close_bracket(&t, &t.brack, '[');
            break;
        case '}':
            col = close_bracket(&t, &t.brace, '{');
            break;
        case '/': {
            char c2 = tok_get(&t, i + 1);
            if (c2 == '/') {
                col = C_COM;
                j = scan_line_comment(t.str, i, t.n);
            } else if (c2 == '*') {
                col = C_COM;
                j = scan_block_comment(t.str, i, t.n);
                if (looks_like_slider_comment(t.str, t.n, i, &slider_spec)) {
                    if (!(E->cursor_idx >= i && E->cursor_idx < j) || (G && G->mb != 0))
                        token_is_slider = true;
                    col = C_SLIDER;
                }
            } else if (pattern_mode) {
                col = C_STR;
                const char *e = skip_path(t.str + i, t.str + t.n);
                if (i > 0 && t.str[i - 1] == '\n') {
                    const char *s = t.str + i;
                    ///////// START OF A NEW PATTERN
                    jump_after_columns();
                    cur_pattern = get_pattern(temp_cstring_from_span(s, e));
                    stbds_arrsetlen(hilites, 0);
                    if (cur_pattern) {
                        int n = stbds_arrlen(cur_pattern->bfs_nodes);
                        // stbds_arrsetcap(hilites, n);
                        for (int ni = 0; ni < n; ++ni) {
                            const token_info_t *se = &cur_pattern->bfs_start_end[ni];
                            float time_since_trigger = (G->iTime - se->last_evaled_glfw_time) * 2.f;
                            if (time_since_trigger >= 0.f && time_since_trigger <= 1.f) {
                                int col = C_HILITE_NOTE;
                                hilite_region_t h = {se->start, se->end, col, (int)((1.f - time_since_trigger) * 256)};
                                stbds_arrpush(hilites, h);
                            }
                        }
                        qsort(hilites, stbds_arrlen(hilites), sizeof(hilite_region_t), compare_hilite_regions);
                        current_hilite_region = 0;
                    }
                }
                j = e - t.str;
                break;
            }
        } break;
        case '<':
            if (wasinclude) {
                col = C_STR;
                j = scan_string(t.str, i, t.n, '>');
            } else
                col = C_PUN;
            break;
        case '"': {
            col = C_STR;
            j = scan_string(t.str, i, t.n, '"');
            if (!pattern_mode && i <= E->cursor_idx && j >= E->cursor_idx) {
                cursor_in_type = 'i';
            }

        } break;
        case '\'': {
            col = C_STR;
            j = scan_string(t.str, i, t.n, '\'');
            if (pattern_mode && E->cursor_idx >= i && E->cursor_idx < j) {
                cursor_in_type = 'c'; // curve
            } else {
                token_is_curve = pattern_mode;
            }
        } break;
        case '#':
            col = C_PREPROC;
            if (!pattern_mode && i + 15 <= t.n && strncmp(t.str + i, "#ifdef PATTERNS", 15) == 0) {
                pattern_mode = 1;
                column_max_width = 0;
                grid_line_start = INVALID_LINE;
                first_grid_line_start = INVALID_LINE;
                grid_line_end = INVALID_LINE;
                pattern_entry_idx = i;
                j = i + 15;
            } else if (pattern_mode && i + 6 <= t.n && strncmp(t.str + i, "#endif", 6) == 0) {
                if (E->cursor_idx >= pattern_entry_idx && E->cursor_idx < i) {
                    E->cursor_in_pattern_area = true;
                }
                jump_after_columns();
                pattern_mode = 0;
                j = i + 6;
            } else if (pattern_mode == 1) {
                if (grid_line_start != INVALID_LINE) {
                    /////////////// END OF A GRID //////////////.
                    if (grid_line_end == INVALID_LINE || t.y > grid_line_end)
                        grid_line_end = t.y;

                    // print_to_screen(t.ptr, left - 3, t.y, empty_bgcol | col, false, "%02x.", (t.y - grid_line_start - 1) & 0xff);
                    grid_line_start = INVALID_LINE;
                    empty_bgcol = C_PATTERN_BG;
                } else {
                    /////////////// START OF A GRID //////////////
                    grid_line_start = t.y;
                    for (int eol = i; eol < t.n; eol++)
                        if (!isspace(t.str[eol])) {
                            break;
                        } else if (t.str[eol] == '\n') {
                            grid_line_start++;
                            break;
                        }
                    if (first_grid_line_start == INVALID_LINE)
                        first_grid_line_start = grid_line_start;
                    else {
                        // second or more column....
                        print_to_screen(t.ptr, t.x, t.y, C_PATTERN_JUMP_MARKER, false, "#");
                        int oldleft = left;

                        grid_line_start = first_grid_line_start;
                        left = column_max_width + 2 - E->intscroll_x;
                        t.y++;
                        add_line(E, oldleft, t.y, left, t.y, C_PATTERN_LINE, 5.f);
                        if (grid_line_end == INVALID_LINE || t.y > grid_line_end)
                            grid_line_end = t.y;
                        stbds_arrpush(column_widths, left);
                        stbds_arrpush(column_widths, t.y);
                        t.y = grid_line_start;
                        t.x = left;
                        empty_bgcol = C_PATTERN_BG;
                    }

                    grid_line_hilight = -1;
                    int remapped_i = (i >= 0 && i < stbds_arrlen(E->new_idx_to_old_idx)) ? E->new_idx_to_old_idx[i] : -1;
                    if (remapped_i >= 0 && cur_pattern) {
                        // find which node in the current pattern this grid corresponds to
                        int n = stbds_arrlen(cur_pattern->bfs_nodes);
                        for (int ni = 0; ni < n; ++ni) {
                            const token_info_t *se = &cur_pattern->bfs_start_end[ni];
                            if (remapped_i >= se->start && remapped_i < se->end) {
                                float t = se->local_time_of_eval;
                                float grid_length = cur_pattern->bfs_min_max_value[ni].mx;
                                float lines_per_cycle = max(1.f, cur_pattern->bfs_min_max_value[ni].mn);
                                if (grid_length > 0 && lines_per_cycle > 0) {
                                    float t_base = floorf(t / grid_length) * grid_length;
                                    t -= t_base;
                                    t *= lines_per_cycle;
                                    grid_line_hilight = (int)t;
                                    // also calculate the haps for this cycle of the pattern
                                    // TODO: pass the relevant seed to get the randoms right...
                                    // viz_haps.s = viz_hap_mem;
                                    // viz_haps.e = viz_hap_mem + 64;
                                    // int hapid = 1;// TODO - pass the right seed
                                    // viz_haps = cur_pattern->_make_haps(viz_haps, 64, -1.f, ni, t_base, t_base + grid_length,
                                    // hapid, false);
                                    // // scale the viz haps' times to lines
                                    // for (hap_t *hap = viz_haps.s; hap < viz_haps.e; hap++) {
                                    //     hap->t0 = (hap->t0 - t_base) * lines_per_cycle;
                                    //     hap->t1 = (hap->t1 - t_base) * lines_per_cycle;
                                    // }
                                }
                            }
                        }
                    }
                }
            }
            break;

        default: {
            if (is_ident_start((unsigned)c)) {
                col = C_DEF;
                j = scan_ident(t.str, i, t.n);
                h = literal_hash_span(t.str + i, t.str + j);
                int midinote = parse_midinote(t.str + i, t.str + j, NULL, 1);
                if (midinote >= 0) {
                    col = C_NOTE;
                } else
                    switch (h) {
#define T(x)                                                                                                                       \
    case HASH(#x):                                                                                                                 \
        col = C_TYPE;                                                                                                              \
        break;
#define K(x)                                                                                                                       \
    case HASH(#x):                                                                                                                 \
        col = C_KW;                                                                                                                \
        break;
#define P(x)                                                                                                                       \
    case HASH(#x):                                                                                                                 \
        col = C_PREPROC;                                                                                                           \
        break;
#define M(x)                                                                                                                       \
    case HASH(#x):                                                                                                                 \
        col = C_KW;                                                                                                                \
        break;
#include "tokens.h"
                        CASE8("S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7") : CASE2("S8", "S9") : {
                            int slideridx = (t.str[i + 1] - '0');
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll_y)
                                slideridx = 16;
                            col = slidercols[slideridx];
                            break;
                        }
                        CASE6("S10", "S11", "S12", "S13", "S14", "S15") : {
                            int slideridx = ((t.str[i + 2] - '0') + 10);
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll_y)
                                slideridx = 16;
                            col = slidercols[slideridx];
                            break;
                        }
                        CASE6("SA", "SB", "SC", "SD", "SE", "SF") : {
                            int slideridx = (t.str[i + 1] - 'A' + 10);
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll_y)
                                slideridx = 16;
                            col = slidercols[slideridx];
                            break;
                        }
                    case HASH("S_"):
                        col = C_SLIDER;
                        break;
                    case HASH("include"):
                        wasinclude = true;
                        col = C_PREPROC;
                        break;
                    default:
                        if (pattern_mode) {
                            Sound *s = get_sound_span(t.str + i, t.str + j);
                            if (s)
                                col = C_SOUND;
                        }
                        break;
                    }
                if (i <= E->cursor_idx && j >= E->cursor_idx) {
                    cursor_in_type = 'i';
                }

            } else {
                j = scan_number(t.str, i, t.n, t.prev_nonspace);
                if (j > i)
                    col = C_NUM;
                else {
                    col = C_PUN;
                }
            }
        } break;
        }
        if (j <= i)
            j = i + 1;
        if (h != HASH("include") && !is_space((unsigned)c))
            wasinclude = false;
        float *curve_data = NULL;
        if (token_is_curve && j > i + 2) {
            int numdata = j - i - 2;
            curve_data = (float *)alloca(4 * numdata);
            fill_curve_data_from_string(curve_data, t.str + i + 1, numdata);
        }
        if (cursor_token_start_idx == cursor_token_end_idx && i <= E->cursor_idx && j >= E->cursor_idx) {
            cursor_token_start_idx = i;
            cursor_token_end_idx = j;
        }
        int starti = i;
        int slider_val = 0;
        if (token_is_slider && slider_spec.maxval != slider_spec.minval && j > i + 2) {
            float v = (slider_spec.curval - slider_spec.minval) / (slider_spec.maxval - slider_spec.minval);
            slider_val = (int)(v * ((j - i - 2) * 8.f - 4.f));
        }
        // PAINT THE TOKEN /////////////////////////////////////////////////
        int istart = i;
        for (; i < j; ++i) {
            char ch = tok_get(&t, i);
            if (i < t.n) {
                E->character_pos[i].x = clamp(t.x + E->intscroll_x, 0, (1 << 10) - 1);
                E->character_pos[i].y = clamp(t.y + E->intscroll_y, 0, (1 << 21) - 1);
                E->character_pos[i].pattern_mode = pattern_mode;
            }
            uint32_t ccol = col;
            if (curve_data) {
                ccol = C_CHART;
                if (i > starti && i < j - 1) {
                    ch = vertical_bar((int)(curve_data[i - starti - 1] * 16.f + 0.5f));
                }
            } else if (token_is_slider && i > starti && i < j - 1) {
                ch = horizontal_tick(slider_val - (i - starti - 1) * 8);
            }
            if (i >= se && i < ee)
                ccol = E->find_mode ? C_SELECTION_FIND_MODE : C_SELECTION;

            if (i == E->cursor_idx) {
                E->cursor_x = t.x + E->intscroll_x;
                E->cursor_y = t.y + E->intscroll_y;
            }
            uint32_t bgcol = ccol & 0xfff00000u;
            if (bgcol == 0 && pattern_mode) {
                ccol |= empty_bgcol;
                // hilight active notes
                if (hilites) {
                    int nh = stbds_arrlen(hilites);
                    int remapped_i = (i >= 0 && i < stbds_arrlen(E->new_idx_to_old_idx)) ? E->new_idx_to_old_idx[i] : -1;
                    if (remapped_i >= 0) {
                        // assumes hilites are sorted by end index
                        while (current_hilite_region < nh && hilites[current_hilite_region].end <= remapped_i)
                            ++current_hilite_region;
                        for (int j = current_hilite_region; j < nh; ++j) {
                            if (remapped_i >= hilites[j].start && remapped_i < hilites[j].end) {
                                ccol = interp_color(ccol, (col & 0xfff00u) | hilites[current_hilite_region].color,
                                                    hilites[current_hilite_region].alpha);
                            }
                            if (hilites[j].start > remapped_i + 32)
                                break; // approximate: breaks for hilites longer than 32
                        }
                    }
                }
            }
            if (t.x < TMW && t.y >= 0 && t.y < TMH)
                t.ptr[t.y * TMW + t.x] = (ccol) | (unsigned char)(ch);
            if (ch == '\t') {
                int nextx = next_tab(t.x - left) + left;
                while (t.x < nextx) {
                    t.ptr[t.y * TMW + t.x] = ccol | (unsigned char)(' ');
                    t.x++;
                }
            } else if (ch == '\n' || ch == 0) {
                // look for an error message
                int ysc = t.y + E->intscroll_y;
                const char *errline = hmget(E->error_msgs, ysc);
                if (errline) {
                    uint32_t errcol = C_ERR;
                    if (strncasecmp(errline, "warning:", 8) == 0)
                        errcol = C_WARNING;
                    for (; *errline && *errline != '\n'; errline++) {
                        if (t.x < TMW && t.y >= 0 && t.y < TMH)
                            t.ptr[t.y * TMW + t.x] = errcol | (unsigned char)(*errline);
                        t.x++;
                    }
                }
                // add any haps that need visualizing
                // if (grid_line_start != INVALID_LINE && viz_haps.e > viz_haps.s) {
                //     int hapy = ysc - grid_line_start - 1;
                //     int linestart = find_start_of_line(E, i);
                //     int lineend = find_end_of_line(E, i);
                //     uint32_t col = empty_bgcol | 0x55500u;
                //     for (hap_t *hap = viz_haps.s; hap < viz_haps.e; hap++) {
                //         if (hap->t0 >= hapy && hap->t0 < hapy + 1) {
                //             const char *note = print_midinote(hap->params[P_NOTE]);
                //             if (spanstr(E->str + linestart, E->str + lineend, note) == 0) { // only show ghost notes if not
                //             already in the line
                //                 t.x+=print_to_screen(t.ptr, t.x, t.y, col, false, " %s", print_midinote(hap->params[P_NOTE]));
                //             }
                //         }
                //     }
                // }
                // fill to the end of the line
                if (pattern_mode && t.y >= 0 && t.y < TMH) {
                    for (; t.x < tmw; t.x++) {
                        t.ptr[t.y * TMW + t.x] = ccol | (unsigned char)(' ');
                    }
                    if (grid_line_start != INVALID_LINE && grid_line_start < t.y) {
                        if (stbds_arrlen(column_widths) == 0) {
                            uint32_t col = (t.y == E->cursor_y - E->intscroll_y) ? 0xeee00u : 0x55500u;
                            print_to_screen(t.ptr, left - 3, t.y, empty_bgcol | col, false, "%02x ",
                                            (t.y - grid_line_start - 1) & 0xff);
                        }
                        if (grid_line_end == INVALID_LINE || t.y > grid_line_end)
                            grid_line_end = t.y;
                    }
                }

                /*
                if (t.y >= 0 && t.y < TMH && sliders[t.y]) {
                    int slider_idx = sliderindices[t.y];
                    float value = *sliders[t.y];
                    int x1 = max(0, tmw - 16);
                    int x2 = tmw;
                    uint32_t col = slidercols[slider_idx];
                    if (closest_slider[slider_idx] == NULL || closest_slider[slider_idx][1] != t.y + E->intscroll)
                        col = C_SLIDER;
                    for (int x = x1; x < x2; x++) {
                        int ch = horizontal_tick((int)(value * 127.f) - (x - x1) * 8 - 2);
                        t.ptr[t.y * TMW + x] = col | ch;
                    }
                }*/
                t.x = left;
                ++t.y;
                // decide on the *next* lines bg color
                if (pattern_mode) {
                    empty_bgcol = C_PATTERN_BG;
                    if (grid_line_start != INVALID_LINE && grid_line_start < t.y) {
                        empty_bgcol = C_TRACKER_BG;
                        int grid_line = t.y - grid_line_start - 1;
                        if ((grid_line % 8) == 0)
                            empty_bgcol = C_TRACKER_BG_8;
                        else if ((grid_line % 4) == 0)
                            empty_bgcol = C_TRACKER_BG_4;
                        if (grid_line == grid_line_hilight && grid_line_hilight >= 0)
                            empty_bgcol = C_TRACKER_BG_HILITE;
                    }
                }
            } else
                ++t.x;
            if (!is_space((unsigned)ch))
                t.prev_nonspace = ch;
            int w = t.x + E->intscroll_x;
            if (w > column_max_width)
                column_max_width = w;
            if (w > E->max_width)
                E->max_width = w;
        }
    }
    E->num_lines = t.y + 1 + E->intscroll_y;
    E->mouse_hovering_chart = false;

    /*
    if (E->cursor_in_pattern_area) {
        // draw a popup below the cursor
        int basex = left;
        int chartx = basex + 13;
        const char *pat_start = find_start_of_pattern(t.str, t.str + E->cursor_idx);
        pat_start = skip_path(pat_start, t.str + t.n);
        const char *pat_end = find_end_of_pattern(pat_start, t.str + t.n);
        int code_start_idx = pat_start - t.str;
        int code_end_idx = pat_end - t.str;
        static uint32_t cached_compiled_string_hash = 0;
        static pattern_maker_t cached_parser = {};
        static hap_t cached_hap_mem[1024];
        static hap_span_t cached_haps = {};
        const char *codes = t.str + code_start_idx;
        const char *codee = t.str + code_end_idx;
        uint32_t hash = fnv1_hash(codes, codee);
        if (hash != cached_compiled_string_hash) {
            cached_compiled_string_hash = hash;
            cached_parser.unalloc();
            cached_parser = {.s = codes, .n = (int)(codee - codes)};
            pattern_t pat = parse_pattern(&cached_parser, 0);
            if (cached_parser.err <= 0) {
                cached_haps = pat.make_haps({cached_hap_mem, cached_hap_mem + 256}, 256, 0.f, 4.f);
            }
        }
        if (!cached_haps.empty()) {
            struct row_key {
                int sound_idx;
                int variant;
                int midinote;
            };
            struct {
                row_key key;
                int value;
            } *rows = NULL;
            int numrows = 0;
            const Node *nodes = cached_parser.nodes;
            int ss = get_select_start(E);
            int se = get_select_end(E);
            for (hap_t *h = cached_haps.s; h < cached_haps.e; h++) {
                if (h->valid_params == 0)
                    continue;
                row_key key = {(h->valid_params & (1 << P_SOUND)) ? (int)h->params[P_SOUND] : -1,
                               (h->valid_params & (1 << P_NUMBER)) ? (int)h->params[P_NUMBER] : -1,
                               (h->valid_params & (1 << P_NOTE)) ? (int)h->params[P_NOTE] : -1};
                int row_plus_one = stbds_hmget(rows, key);
                if (!row_plus_one) {
                    row_plus_one = ++numrows;
                    if (numrows > 16)
                        break;
                    stbds_hmput(rows, key, row_plus_one);
                    int y = E->cursor_y - E->intscroll + row_plus_one;
                    if (y >= 0 && y < TMH) {
                        Sound *sound = h->valid_params & (1 << P_SOUND) ? get_sound_by_index(key.sound_idx) : NULL;
                        const char *sound_name = sound ? sound->name : "";
                        char hapstr[64];
                        char *s = hapstr;
                        char *e = s + sizeof(hapstr) - 1;
                        s += snprintf(s, e - s, "%s", sound_name);
                        if (h->valid_params & (1 << P_NUMBER)) {
                            s += snprintf(s, e - s, ":%d", key.variant);
                        }
                        if (h->valid_params & (1 << P_NOTE)) {
                            if (s != hapstr && s != e)
                                *s++ = ' ';
                            s += snprintf(s, e - s, "%s", print_midinote(key.midinote));
                        }
                        if (s != e)
                            *s++ = ' ';
                        print_to_screen(t.ptr, chartx - (s - hapstr), y, 0x11188800, false, "%s", hapstr);
                        print_to_screen(t.ptr, chartx + 128, y, 0x11188800, false, " %s", hapstr);
                        for (int x = 0; x < 128; ++x) {
                            uint32_t col = ((x & 31) == 0) ? C_CHART_HILITE : (x & 8) ? C_CHART : C_CHART_HOVER;
                            t.ptr[y * TMW + x + chartx] = col | (unsigned char)(' ');
                        }
                    }
                }
                // draw the hap h
                int y = E->cursor_y - E->intscroll + row_plus_one;
                int hapstart_idx = nodes[h->node].start + code_start_idx;
                int hapend_idx = nodes[h->node].end + code_start_idx;
                uint32_t hapcol = 0; // 0 = leave attribute as is.
                if (ss <= hapstart_idx && se >= hapend_idx)
                    hapcol = C_SELECTION;
                else if (hapstart_idx <= E->cursor_idx && hapend_idx >= E->cursor_idx)
                    hapcol = C_CHART_HILITE;
                int hapx1 = (int)((h->t0 * 32.f));
                int hapx2 = (int)((h->t1 * 32.f));
                for (int x = hapx1; x < hapx2; ++x) {
                    if (x >= 0 && x < 128) {
                        uint32_t charcol = hapcol ? hapcol : t.ptr[y * TMW + x + chartx] & 0xffffff00u;
                        t.ptr[y * TMW + x + chartx] = charcol | (unsigned char)((x == hapx1)       ? 128 + 16 + 2
                                                                                : (x == hapx2 - 1) ? '>'
                                                                                                   : 128 + 16 + 15);
                    }
                }
            }
            stbds_hmfree(rows);
        }
        if (cached_parser.err) {
            print_to_screen(t.ptr, basex + cached_parser.err, E->cursor_y + 1 - E->intscroll, C_ERR, false, cached_parser.errmsg);
        }
    }
    */
    E->autocomplete_index = 0;
    stbds_hmfree(E->autocomplete_options);
    if (cursor_in_type == 'i' && E->find_mode == 0 && G->iTime > E->autocomplete_show_after &&
        cursor_token_start_idx < cursor_token_end_idx) {
        // autocomplete popup
        const char *token = &t.str[cursor_token_start_idx];
        int pm = E->cursor_in_pattern_area;
        if (token[0] == '"') {
            token++;
            cursor_token_start_idx++;
            pm = true;
        }
        int include_chord_names = 0;
        if (pm) {
            // if the start of the token parses as a midi note, then skip forward
            const char *noteend = NULL;
            const char *tokenend = &t.str[cursor_token_end_idx];
            int note = parse_midinote(token, tokenend, &noteend, 0);
            if (note >= 0 && noteend < tokenend) {
                include_chord_names = noteend - token;
            }
        }
        int token_len = cursor_token_end_idx - cursor_token_start_idx;
        int num_chars_so_far = E->cursor_idx - cursor_token_start_idx;
        int x = 0, y = 0;
        bool gotxy = idx_to_xy(E, cursor_token_start_idx, &x, &y);
        x -= E->intscroll_x;
        y -= E->intscroll_y;
        if (gotxy && (num_chars_so_far >= 2 || include_chord_names)) { // at least 2 chars

            int bestscore = 2;
            char *bestoption = NULL;

#define COMPARE_PREFIX(x, tokofs)                                                                                                  \
    {                                                                                                                              \
        autocomplete_option_t score = autocomplete_score(token + tokofs, token_len - tokofs, num_chars_so_far - tokofs, x);        \
        if (score.value >= 2) {                                                                                                    \
            stbds_hmputs(E->autocomplete_options, score);                                                                          \
            if (score.value > bestscore) {                                                                                         \
                bestscore = score.value;                                                                                           \
                bestoption = (char *)x;                                                                                            \
            }                                                                                                                      \
        }                                                                                                                          \
    }
#define K(x)                                                                                                                       \
    if (!pm)                                                                                                                       \
        COMPARE_PREFIX(#x, 0);
#define T(x)                                                                                                                       \
    if (!pm)                                                                                                                       \
        COMPARE_PREFIX(#x, 0);
#define P(x)                                                                                                                       \
    if (!pm)                                                                                                                       \
        COMPARE_PREFIX(#x, 0);
#define M(x)                                                                                                                       \
    if (pm)                                                                                                                        \
        COMPARE_PREFIX(#x, 0);
#include "tokens.h"
            if (pm) {
                if (include_chord_names) {
#define X(scale_name, scale_bits) COMPARE_PREFIX(scale_name, include_chord_names);
#include "scales.h"
                }
                int n = num_sounds();
                for (int i = 0; i < n; ++i) {
                    char *name = G->sounds[i].key;
                    COMPARE_PREFIX(name, 0);
                }
            }
            int numchoices = stbds_hmlen(E->autocomplete_options);
            if (numchoices == 0) {
                stbds_hmfree(E->autocomplete_options);
                E->autocomplete_options = NULL;
                E->autocomplete_index = 0;
                E->autocomplete_scroll_y = 0;
            } else {
                int avoid_y = y;
                int unscrolled_besti = stbds_hmgeti(E->autocomplete_options, bestoption);
                int besti = unscrolled_besti + E->autocomplete_scroll_y;
                if (besti >= numchoices)
                    besti = numchoices - 1;
                if (besti < 0)
                    besti = 0;
                E->autocomplete_scroll_y = besti - unscrolled_besti;
                E->autocomplete_index = besti;
                y -= besti;
                for (int i = 0; i < numchoices; ++i) {
                    if (y == avoid_y)
                        y++;
                    if (y >= TMH)
                        break;
                    if (y >= 0) {
                        autocomplete_option_t *o = &E->autocomplete_options[i];
                        if (o->matchlen == strlen(o->key))
                            continue;
                        if (y == avoid_y + 1) {
                            int xx = x + left - o->xoffset;
                            int idx = 0;
                            for (const char *c = o->key; *c; c++, ++idx) {
                                bool match = idx >= o->xoffset && idx < o->xoffset + o->matchlen;
                                if (xx >= 0 && xx < TMW && y >= 0 && y < TMH)
                                    t.ptr[xx + y * TMW] = (match ? C_SELECTION : C_AUTOCOMPLETE) | (unsigned char)(*c);
                                xx++;
                            }
                        } else {
                            print_to_screen(t.ptr, x + left - o->xoffset, y, C_AUTOCOMPLETE_SECONDARY, false, o->key);
                        }
                    }
                    y++;
                }
            }
        }
        // print_to_screen(t.ptr, E->cursor_x, E->cursor_y + 1 - E->intscroll, C_SELECTION, false, "autocomplete popup");
    }
    // draw a popup Above the cursor if its inside a curve
    if (cursor_in_type == 'c' && cursor_token_end_idx - cursor_token_start_idx >= 2) {
        // trim the '
        int cursor_in_curve_start_idx = cursor_token_start_idx + 1;
        int cursor_in_curve_end_idx = cursor_token_end_idx - 1;
        int x, y;
        idx_to_xy(E, cursor_in_curve_start_idx, &x, &y);
        x += left;
        int datalen = max(0, cursor_in_curve_end_idx - cursor_in_curve_start_idx);
        // work out bounding box also
        float x1 = x * E->font_width;
        float x2 = x1 + datalen * E->font_width;
        float y1 = (E->cursor_y - 4) * E->font_height - E->scroll_y;
        float y2 = y1 + 4 * E->font_height;
        uint32_t col = C_CHART;
        const float margin = E->font_width;
        if (G->mx >= x1 - margin && G->mx <= x2 + margin && G->my >= y1 - margin && G->my <= y2) {
            col = C_CHART_HOVER;
            E->mouse_hovering_chart = true;
        }
        bool click_interaction = (E->mouse_dragging_chart && (G && (G->mb & 1))) || (E->mouse_clicked_chart);
        if (click_interaction && G) {
            col = C_CHART_DRAG;
            int mx = (G->mx - x1) / E->font_width;
            bool mouse_click_down = !(G->old_mb & 1);
            if (mouse_click_down) {
                E->mouse_click_original_char = ' ';
                E->mouse_click_original_char_x = mx;
            }
            float value = ((y2 - G->my) / E->font_height) * 16.f;
            value = clamp(value, 0.f, 64.f);
            if (mx >= 0 && mx < datalen) {
                // TODO: edit op
                char *ch = &E->str[cursor_in_curve_start_idx + mx];
                if (mouse_click_down)
                    E->mouse_click_original_char = *ch;
                if (E->mouse_clicked_chart && E->mouse_click_original_char != ' ' && E->mouse_click_original_char_x == mx)
                    *ch = ' ';
                else
                    *ch = btoa_tab[(int)value];
                E->cursor_idx = cursor_in_curve_start_idx + mx;
                E->select_idx = E->cursor_idx;
            }
        }
        int ss = get_select_start(E);
        int se = get_select_end(E);
        float data[datalen];
        fill_curve_data_from_string(data, t.str + cursor_in_curve_start_idx, datalen);
        for (int i = 0; i < datalen; i++) {
            int xx = x + i;
            int idx = cursor_in_curve_start_idx + i;
            int datay = (int)(data[i] * 64.f + 0.5f);
            uint32_t ccol = col;
            if (xx == E->cursor_x)
                ccol = C_CHART_HILITE;
            if (idx >= ss && idx < se)
                ccol = C_SELECTION;
            if (xx >= 0 && xx < TMW)
                for (int j = 0; j < 4; ++j) {
                    int y = E->cursor_y - 1 - j - E->intscroll_y;
                    if (y >= 0 && y < TMH) {
                        int ch = vertical_bar(datay);
                        t.ptr[y * TMW + xx] = ccol | (unsigned char)(ch); // draw a bar graph cellchar
                    }
                    datay -= 16;
                }
        } // graph drawing loop
    } // curve popup
    E->mouse_clicked_chart = false;

    stbds_arrfree(hilites);
    stbds_arrfree(column_widths);
    return E->num_lines;
}

void load_file_into_editor(EditorState *E) {
    if (E->font_width <= 0 || E->font_height <= 0) {
        E->font_width = 12;
        E->font_height = 24;
    }
    stbds_arrfree(E->str);
    stbds_arrfree(E->new_idx_to_old_idx);
    E->str = load_file(E->fname);
    init_remapping(E);
}
