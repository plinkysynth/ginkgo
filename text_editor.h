#pragma once

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
} EditorState;

char *make_cstring_from_span(const char *str, int start, int end, int alloc_extra) {
    int len = end - start;
    if (len <= 0)
        return NULL;
    char *cstr = calloc(1, len + 1 + alloc_extra);
    memcpy(cstr, str + start, len);
    return cstr;
}

edit_op_t apply_edit_op(EditorState *E, edit_op_t op, int update_cursor_idx) {
    int old_cursor_idx = E->cursor_idx;
    int old_select_idx = E->select_idx;
    char *removed_str = NULL;
    if (op.remove_end > op.remove_start) {
        removed_str = make_cstring_from_span(E->str, op.remove_start, op.remove_end, 0);
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
        free(E->edit_ops[i].insert_str);
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
    free(undo_op.insert_str);
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

void editor_click(EditorState *E, float x, float y, int is_drag) {
    int cx = (int)(x / E->font_width + 0.5f);
    int cy = (int)(y / E->font_height);
    E->cursor_idx = xy_to_idx(E, cx, cy);
    if (!is_drag)
        E->select_idx = E->cursor_idx;
    idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
    E->cursor_x_target = E->cursor_x;
}

static inline bool isspaceortab(char c) { return c==' ' || c=='\t'; }

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
        case GLFW_KEY_C:
        case GLFW_KEY_X: {
            int se = get_select_start(E);
            int ee = get_select_end(E);
            char *str = make_cstring_from_span(E->str, se, ee, 0);
            if (str) {
                glfwSetClipboardString(win, str);
                free(str);
                if ((key & 0xffff) == GLFW_KEY_X) {
                    push_edit_op(E, se, ee, NULL, 1);
                }
            }
            break;
        }
        case GLFW_KEY_V: {
            const char *str = glfwGetClipboardString(win);
            if (str) {
                push_edit_op(E, get_select_start(E), get_select_end(E), str, 1);
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
                char *str = make_cstring_from_span(E->str, ss, se, ((ey-sy)+1)*4);
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
                // E->select_idx = ss;
                // E->cursor_idx = ss+strlen(str);
                free(str);
                break;
            }        
        break; }
        }
    switch (key & 0xFFFF) {
    case '\b':
    case GLFW_KEY_BACKSPACE:
        if (has_selection) {
            push_edit_op(E, get_select_start(E), get_select_end(E), NULL, 1);
        } else if (E->cursor_idx > 0) {
            push_edit_op(E, E->cursor_idx - 1, E->cursor_idx, NULL, 1);
        }
        break;
    case GLFW_KEY_DELETE:
        push_edit_op(E, E->cursor_idx, E->cursor_idx + 1, NULL, 1);
        break;
    // TODO: tab/shift-tab should indent or unindent the whole selection; unless its tab after leading space, in which case its just
    // an insert.
    case '\t': {
        int ss = get_select_start(E);
        int se = get_select_end(E);
        int sy = idx_to_y(E, ss), ey = idx_to_y(E, se);
        if (sy==ey && !shift) goto insert_character;
        ss = xy_to_idx(E, 0, sy);
        se = xy_to_idx(E, 0x7fffffff, ey);
        char *str = make_cstring_from_span(E->str, ss, se, ((ey-sy)+1)*4);
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
        free(str);
        break;
    }        
    case '\n':
    case ' ' ... '~':
insert_character:
        if (!super && !ctrl) {
            // delete the selection; insert the character
            int ls = (key == '\n') ? count_leading_spaces(E, xy_to_idx(E, 0, E->cursor_y)) : 0;
            char *buf = alloca(ls + 2);
            buf[0] = key;
            memset(buf + 1, ' ', ls);
            buf[ls + 1] = 0;
            push_edit_op(E, get_select_start(E), get_select_end(E), buf, 1);
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
        E->cursor_idx = super ? 0 : xy_to_idx(E, E->cursor_x_target, E->cursor_y - 1);
        set_target_x = 0;
        reset_selection = !shift;
        break;
    case GLFW_KEY_DOWN:
        E->cursor_idx = super ? n : xy_to_idx(E, E->cursor_x_target, E->cursor_y + 1);
        set_target_x = 0;
        reset_selection = !shift;
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
        E->cursor_idx = super ? 0 : xy_to_idx(E, E->cursor_x_target, E->cursor_y - 20);
        set_target_x = 0;
        reset_selection = !shift;
        break;
    case GLFW_KEY_PAGE_DOWN:
        E->cursor_idx = super ? n : xy_to_idx(E, E->cursor_x_target, E->cursor_y + 20);
        set_target_x = 0;
        reset_selection = !shift;
        break;
    }
    if (reset_selection)
        E->select_idx = E->cursor_idx;
    idx_to_xy(E, E->cursor_idx, &E->cursor_x, &E->cursor_y);
    if (set_target_x)
        E->cursor_x_target = E->cursor_x;
}
