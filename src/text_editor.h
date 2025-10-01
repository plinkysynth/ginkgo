#pragma once

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

typedef struct EditorState {
    char *str;           // stb stretchy buffer
    char *fname;         // name of the file
    edit_op_t *edit_ops; // stretchy buffer
    int undo_idx;
    bool is_shader; // is this a shader file?
    bool mouse_hovering_chart;
    bool find_mode;
    bool cursor_in_pattern_area;
    bool mouse_dragging_chart;
    bool mouse_clicked_chart;
    char mouse_click_original_char;
    int mouse_click_original_char_x;
    float scroll_y;
    float scroll_y_target;
    int intscroll; // how many lines we scrolled.
    int cursor_x;
    int cursor_y;
    int cursor_x_target;
    int cursor_idx;
    int select_idx;
    int num_lines;
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

typedef struct slider_spec_t {
    int start_idx;
    int end_idx;
    int value_start_idx;
    int value_end_idx;
    float minval, maxval, curval;
} slider_spec_t;

bool ispartofnumber(char c) { return isdigit(c) || c == '-' || c == '.' || c == '+' || c == 'e' || c == 'E'; }

int try_parse_number(const char *str, int n, int idx, float *out, int *out_idx, float default_val) {
    int i = idx;
    while (i < n && ispartofnumber(str[i]))
        ++i;
    if (i == idx) {
        *out = default_val;
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
        *out_idx = i;
        return 0;
    }
    *out = val;
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
        removed_str = make_cstring_from_span(E->str + op.remove_start, E->str + op.remove_end, 0);
        stbds_arrdeln(E->str, op.remove_start, op.remove_end - op.remove_start);
    }
    int numins = op.insert_str ? strlen(op.insert_str) : 0;
    if (numins) {
        stbds_arrinsn(E->str, op.remove_start, numins);
        memcpy(E->str + op.remove_start, op.insert_str, numins);
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
    edit_op_t op = {mini(remove_start, remove_end), maxi(remove_start, remove_end), (char *)insert_str};
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

static inline int get_select_start(EditorState *obj) { return mini(obj->cursor_idx, obj->select_idx); }
static inline int get_select_end(EditorState *obj) { return maxi(obj->cursor_idx, obj->select_idx); }

static inline int next_tab(int x) { return (x + 4) & ~3; }

int xy_to_idx(EditorState *E, int tx, int ty) {
    tx = maxi(0, tx);
    ty = maxi(0, ty);
    int n = stbds_arrlen(E->str);
    int x = 0, y = 0;
    for (int i = 0; i < n; i++) {
        if (x == tx && y == ty)
            return i;
        if (E->str[i] == '\n' || E->str[i] == 0) {
            if (y == ty)
                return i;
            y++;
            x = 0;
            continue;
        }
        if (E->str[i] == '\t') {
            x = next_tab(x);
            continue;
        }
        x++;
    }
    return n;
}

int idx_to_x(EditorState *E, int idx, int line_start_idx) {
    int x = 0;
    for (int i = line_start_idx; i < idx; i++) {
        if (E->str[i] == '\n' || E->str[i] == 0)
            break;
        if (E->str[i] == '\t')
            x = next_tab(x);
        else
            x++;
    }
    return x;
}

void idx_to_xy(EditorState *E, int idx, int *tx, int *ty) {
    int x = 0, y = 0;
    for (int i = 0; i < idx; i++) {
        if (i == idx)
            break;
        if (E->str[i] == '\n' || E->str[i] == 0) {
            y++;
            x = 0;
            continue;
        }
        if (E->str[i] == '\t') {
            x = next_tab(x);
            continue;
        }
        x++;
    }
    *tx = x;
    *ty = y;
}

static int idx_to_y(EditorState *E, int idx) {
    int x = 0, y = 0;
    idx_to_xy(E, idx, &x, &y);
    return y;
}

static void adjust_font_size(EditorState *E, int delta) {
    float yzoom = E->cursor_y;
    E->scroll_y_target = E->scroll_y_target / E->font_height - yzoom;
    E->scroll_y = E->scroll_y / E->font_height - yzoom;
    E->font_width = clampi(E->font_width + delta, 8, 256);
    E->font_height = E->font_width * 2;
    E->scroll_y_target = (E->scroll_y_target + yzoom) * E->font_height;
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

static inline bool isseparator(char c) { return c==' ' || c=='\t' || c=='\n' || c=='\r' || c==';' || c==',' || c==':' || c=='.' || c=='(' || c==')' || c=='[' || c==']' || c=='{' || c=='}' || c=='\'' || c=='\"' || c=='`'; }
static inline bool isnewline(char c) { return c=='\n' || c=='\r'; }

void editor_click(EditorState *E, basic_state_t *G, float x, float y, int is_drag, int click_count) {
    y += E->scroll_y;
    int tmw = (fbw-64.f) / E->font_width;
    float fx = (x / E->font_width + 0.5f);
    float fy = (y / E->font_height);
    int cx = (int)fx;
    int cy = (int)fy;
    if (!is_drag) {
        E->click_fx = fx;
        E->click_fy = fy;
        E->mouse_dragging_chart = E->mouse_hovering_chart;
    }        
    if (E->mouse_hovering_chart && is_drag<0) {
        E->mouse_clicked_chart = click_count > 0;
    }
    if (!E->mouse_dragging_chart) {
        // sliders on the right interaction
        if (G && E->click_fx >= tmw-16 && E->click_fx < tmw) {
            for (int slideridx=0;slideridx<16;++slideridx) {
                for (int i=0;i<G->sliders[slideridx].n;i+=2) {
                    int line = G->sliders[slideridx].data[i+1] - E->intscroll;
                    if (line==(int)E->click_fy) {
                        if (!is_drag) {
                            E->click_slider_value = G->sliders[slideridx].data[i];
                        } else {
                            float newvalue = clampf(E->click_slider_value + (fx-E->click_fx)/16.f, 0.f, 1.f);
                            G->sliders[slideridx].data[i] = newvalue;
                        }
                        return;
                    }
                }
            }
        }
        // text editor mouse interaction
        // adjust for the left margin of the code view
        int left = 64 / E->font_width;
        cx -= left;

        //int looks_like_slider_comment(const char *str, int n, int idx,slider_spec_t *out) { // see if 'idx' is inside a /*0======5*/<whitespace>number type comment.
        int click_idx = xy_to_idx(E, cx, cy);
        if (!is_drag) {
            E->click_down_idx = click_idx;
        }
        slider_spec_t slider_spec;
        if (looks_like_slider_comment(E->str, stbds_arrlen(E->str), E->click_down_idx, &slider_spec)) {
            
            int slider_x1, slider_x2, slider_y;
            idx_to_xy(E, slider_spec.start_idx, &slider_x1, &slider_y);
            idx_to_xy(E, slider_spec.end_idx, &slider_x2, &slider_y);
            if (!is_drag) {
                E->click_slider_value = slider_spec.curval;
            }
            float dv = fx - E->click_fx;
            float v = E->click_slider_value + (slider_spec.maxval - slider_spec.minval) * (dv / (slider_x2-slider_x1));
            v=clampf(v, slider_spec.minval, slider_spec.maxval);
            //printf("slider value: %f\n", v);
            if (v!=slider_spec.curval) {
                char buf[32];
                int numdecimals = clampi(3.f - log10f(slider_spec.maxval - slider_spec.minval), 0, 5);
                char fmtbuf[32];
                snprintf(fmtbuf, sizeof(fmtbuf), "%%0.%df", numdecimals);
                snprintf(buf, sizeof(buf), fmtbuf, v);
                char *end = buf + strlen(buf);
                if (strrchr(buf,'.')) while (end>buf && end[-1]=='0') --end;
                *end = 0;
                // TODO : undo merging. for now, just poke the text in directly.
                stbds_arrdeln(E->str, slider_spec.value_start_idx, slider_spec.value_end_idx - slider_spec.value_start_idx);
                stbds_arrinsn(E->str, slider_spec.value_start_idx, strlen(buf));
                memcpy(E->str + slider_spec.value_start_idx, buf, strlen(buf));
            }
        } else {

            E->cursor_idx = click_idx;
            if (!is_drag)
                E->select_idx = E->cursor_idx;
            idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
            E->cursor_x_target = E->cursor_x;
            if (is_drag<0 && click_count==2) {
                // double click - select word
                int idx,idx2;
                for (idx = E->cursor_idx; idx<stbds_arrlen(E->str); ++idx) if (isseparator(E->str[idx])) break;
                for (idx2 = E->cursor_idx; idx2>=0; --idx2) if (isseparator(E->str[idx2])) break;
                E->select_idx = idx2+1;
                E->cursor_idx = idx;
            }
            if (is_drag<0 && click_count==3) {
                // triple click - select line
                int idx,idx2;
                for (idx = E->cursor_idx; idx<stbds_arrlen(E->str); ++idx) if (isnewline(E->str[idx])) break;
                for (idx2 = E->cursor_idx; idx2>=0; --idx2) if (isnewline(E->str[idx2])) break;
                E->select_idx = idx2+1;
                E->cursor_idx = idx;
            }
        }
    }
}

static inline bool isspaceortab(char c) { return c==' ' || c=='\t'; }

int jump_to_found_text(EditorState *E, int backwards, int extra_char) {
    int delta = backwards ? -1 : 1;
    int ss = get_select_start(E);
    int se = get_select_end(E);
    const char *needle = E->str + ss;
    int needle_len = se - ss;
    if (!extra_char) { ss+=delta; se+=delta; }
    int n = stbds_arrlen(E->str);
    if (extra_char) n--;
    while (ss>=0 && se<=n) {
        if (strncmp(needle, E->str + ss, needle_len) == 0 && (extra_char==0 || E->str[ss+needle_len]==extra_char)) {
            E->select_idx = ss;
            E->cursor_idx = ss + needle_len + (extra_char ? 1 : 0);
            return 1;
        }
        ss+=delta; se+=delta;
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
        } else {
            E->select_idx = E->cursor_idx;
        }
    }

    if (super)
        switch (key & 0xFFFF) {
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
        }
        break;
        case GLFW_KEY_C:
        case GLFW_KEY_X: {
            int se = get_select_start(E);
            int ee = get_select_end(E);
            char *str = make_cstring_from_span(E->str + se, E->str + ee, 0);
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
                int sy = idx_to_y(E, ss), ey = idx_to_y(E, se);
                ss = xy_to_idx(E, 0, sy);
                se = xy_to_idx(E, 0x7fffffff, ey);
                char *str = make_cstring_from_span(E->str + ss, E->str + se, ((ey-sy)+1)*4);
                int minx = 0x7fffffff;
                int all_commented = true;
                int n = se-ss;
                for (int idx = 0; idx<n; ) {
                    int x=0;
                    while (idx<n && isspaceortab(str[idx])) { if (str[idx]=='\t') x=next_tab(x); else ++x; ++idx; }
                    if (idx<n && str[idx]=='\n') { ++idx; continue; } // skip blank lines
                    minx=mini(x, minx);
                    if (idx+2>=n || str[idx]!='/' || str[idx+1]!='/') all_commented=false;
                    while (idx<n) { ++idx; if (str[idx-1]=='\n') break; }
                }
                if (all_commented) { // remove the comments!
                    int dstidx=0;
                    for (int idx = 0; idx<n;) {
                        while (idx<n && isspaceortab(str[idx])) str[dstidx++]=str[idx++];
                        if (idx+2<n && str[idx]=='/' && str[idx+1]=='/') {
                            int oldidx=idx;
                            idx+=2;
                            if (idx<n && isspaceortab(str[idx])) idx++;
                            if (idx<E->cursor_idx) E->cursor_idx+=oldidx-idx;
                            if (idx<E->select_idx) E->select_idx+=oldidx-idx;

                        }
                        while (idx<n) { str[dstidx++]=str[idx++]; if (str[dstidx-1]=='\n') break; }
                    }
                    str[dstidx]=0;
                } else { // add comments at minx
                    for (int idx = 0; idx<n;) {
                        int x=0;
                        while (idx<n && x<minx && isspaceortab(str[idx])) { if (str[idx]=='\t') x=next_tab(x); else ++x; ++idx; }
                        if (idx<n && str[idx]!='\n') {
                            memmove(str+idx+3, str+idx, n-idx);
                            str[idx++]='/';
                            str[idx++]='/';
                            str[idx++]=' ';
                            n+=3;
                            if (idx<=E->cursor_idx) E->cursor_idx+=3;
                            if (idx<=E->select_idx) E->select_idx+=3;
                        }
                        while (idx<n) { ++idx; if (str[idx-1]=='\n') break; }
                    }
                    str[n]=0;
                    
                }
                push_edit_op(E, ss, se, str, 0);
                E->find_mode = false;
                if (sy!=ey) {
                    E->select_idx = ss;
                    E->cursor_idx = ss+strlen(str);
                }
                stbds_arrfree(str);
                break;
            }        
        break; }
        }
    switch (key & 0xFFFF) {
    case '\b':
    case GLFW_KEY_BACKSPACE:
        if (E->find_mode) {
            int ss = get_select_start(E);
            int se = get_select_end(E);
            E->select_idx = ss;
            E->cursor_idx = maxi(ss,se-1);
            break;
        }
        else if (has_selection) {
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
    // TODO: tab/shift-tab should indent or unindent the whole selection; unless its tab after leading space, in which case its just
    // an insert.
    case '\t': {
        if (E->find_mode) {
            jump_to_found_text(E, shift, 0);
        } else {
            int ss = get_select_start(E);
            int se = get_select_end(E);
            int sy = idx_to_y(E, ss), ey = idx_to_y(E, se);
            if (sy==ey && !shift) goto insert_character;
            ss = xy_to_idx(E, 0, sy);
            se = xy_to_idx(E, 0x7fffffff, ey);
            char *str = make_cstring_from_span(E->str + ss, E->str +se, ((ey-sy)+1)*4);
            for (char *c=str;*c;) {
                if (shift) {
                    // skip up to 4 spaces or a tab.
                    char *nextchar=c;
                    for (int i=0;i<4;++i,++nextchar) if (*nextchar=='\t') { nextchar++; break;} else if (*nextchar!=' ') break;
                    memmove(c, nextchar, strlen(nextchar)+1);
                } else {
                    memmove(c+4, c, strlen(c)+1);
                    memset(c,' ', 4);
                }
                while (*c && *c!='\n') ++c;
                if (*c=='\n') ++c;
            }
            push_edit_op(E, ss, se, str, 1);
            E->select_idx = ss;
            E->cursor_idx = ss+strlen(str);
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
                // delete the selection; insert the character
                int ls = (key == '\n') ? count_leading_spaces(E, xy_to_idx(E, 0, E->cursor_y)) : 0;
                char buf[ls+2];
                buf[0] = key;
                memset(buf + 1, ' ', ls);
                buf[ls + 1] = 0;
                push_edit_op(E, get_select_start(E), get_select_end(E), buf, 1);
            }
        }
        break;
    case GLFW_KEY_LEFT:
        if (super) {
            // same as home
            int ls = count_leading_spaces(E, xy_to_idx(E, 0, E->cursor_y));
            E->cursor_idx = xy_to_idx(E, (E->cursor_x > ls) ? ls : 0, E->cursor_y);
        } else if (has_selection && !shift) {
            E->cursor_idx = get_select_start(E);
        } else
            E->cursor_idx = maxi(0, E->cursor_idx - 1);
        reset_selection = !shift;
        break;
    case GLFW_KEY_RIGHT:
        if (super) {
            // same as end
            E->cursor_idx = xy_to_idx(E, 0x7fffffff, E->cursor_y);
        } else if (has_selection && !shift) {
            E->cursor_idx = get_select_end(E);
        } else
            E->cursor_idx = mini(E->cursor_idx + 1, n);
        reset_selection = !shift;
        break;

    case GLFW_KEY_UP:
        if (E->find_mode) {
            jump_to_found_text(E, 1, 0);
        } else {
            E->cursor_idx = super ? 0 : xy_to_idx(E, E->cursor_x_target, E->cursor_y - 1);
            set_target_x = 0;
            reset_selection = !shift;
        }
        break;
    case GLFW_KEY_DOWN:
        if (E->find_mode) {
            jump_to_found_text(E, 0, 0);
        } else {
            E->cursor_idx = super ? n : xy_to_idx(E, E->cursor_x_target, E->cursor_y + 1);
            set_target_x = 0;
            reset_selection = !shift;
        }
        break;
    case GLFW_KEY_HOME: {
        int ls = count_leading_spaces(E, xy_to_idx(E, 0, E->cursor_y));
        E->cursor_idx = xy_to_idx(E, (E->cursor_x > ls) ? ls : 0, E->cursor_y);
        reset_selection = !shift;
        break;
    }
    case GLFW_KEY_END:
        E->cursor_idx = xy_to_idx(E, 0x7fffffff, E->cursor_y);
        reset_selection = !shift;
        break;
    case GLFW_KEY_PAGE_UP:
        if (E->find_mode) {
            jump_to_found_text(E, 1, 0);
        } else {
            E->cursor_idx = super ? 0 : xy_to_idx(E, E->cursor_x_target, E->cursor_y - 20);
            set_target_x = 0;
            reset_selection = !shift;
        }
        break;
    case GLFW_KEY_PAGE_DOWN:
        if (E->find_mode) {
            jump_to_found_text(E, 0, 0);
        } else {
            E->cursor_idx = super ? n : xy_to_idx(E, E->cursor_x_target, E->cursor_y + 20);
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
