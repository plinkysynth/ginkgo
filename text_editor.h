#pragma once

typedef struct EditorState EditorState;
float STB_TEXTEDIT_GETWIDTH(EditorState *obj, int n, int i);
int STB_TEXTEDIT_INSERTCHARS(EditorState *obj, int i, const char *c, int n);
#define STB_TEXTEDIT_CHARTYPE char
#define STB_TEXTEDIT_POSITIONTYPE int
#define STB_TEXTEDIT_UNDOSTATECOUNT 99
#define STB_TEXTEDIT_UNDOCHARCOUNT 999
#define STB_TEXTEDIT_STRING EditorState
#define STB_TEXTEDIT_STRINGLEN(obj) stbds_arrlen(obj->str)
#define STB_TEXTEDIT_GETWIDTH_NEWLINE -1.f
#define STB_TEXTEDIT_NEWLINE '\n'
#define STB_TEXTEDIT_GETCHAR(obj, i) obj->str[i]
#define STB_TEXTEDIT_DELETECHARS(obj, i, n) stbds_arrdeln(obj->str, i, n)
#define STB_TEXTEDIT_K_SHIFT (GLFW_MOD_SHIFT << 16)
#define STB_TEXTEDIT_K_LEFT GLFW_KEY_LEFT
#define STB_TEXTEDIT_K_RIGHT GLFW_KEY_RIGHT
#define STB_TEXTEDIT_K_UP GLFW_KEY_UP
#define STB_TEXTEDIT_K_DOWN GLFW_KEY_DOWN
#define STB_TEXTEDIT_K_PGUP GLFW_KEY_PAGE_UP
#define STB_TEXTEDIT_K_PGDOWN GLFW_KEY_PAGE_DOWN
#define STB_TEXTEDIT_K_LINESTART GLFW_KEY_HOME
#define STB_TEXTEDIT_K_LINEEND GLFW_KEY_END
#define STB_TEXTEDIT_K_TEXTSTART (GLFW_KEY_HOME | (GLFW_MOD_SUPER << 16))
#define STB_TEXTEDIT_K_TEXTEND (GLFW_KEY_END | (GLFW_MOD_SUPER << 16))
#define STB_TEXTEDIT_K_DELETE GLFW_KEY_DELETE
#define STB_TEXTEDIT_K_BACKSPACE GLFW_KEY_BACKSPACE
#define STB_TEXTEDIT_K_UNDO GLFW_KEY_Z | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_REDO GLFW_KEY_Y | (GLFW_MOD_SUPER << 16)
// #define STB_TEXTEDIT_K_WORDLEFT GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
// #define STB_TEXTEDIT_K_WORDRIGHT GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_LINESTART2 GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_LINEEND2 GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_TEXTSTART2 (GLFW_KEY_UP | (GLFW_MOD_SUPER << 16))
#define STB_TEXTEDIT_K_TEXTEND2 (GLFW_KEY_DOWN | (GLFW_MOD_SUPER << 16))

#include "3rdparty/stb_textedit.h"

typedef struct error_msg_t {
    int key;           // a line number
    const char *value; // a line of text (terminated by \n)
} error_msg_t;

typedef struct EditorState {
    char *str; // stb stretchy buffer
    char *fname; // name of the file
    bool is_shader; // is this a shader file?
    float scroll_y;
    float scroll_y_target;
    int intscroll; // how many lines we scrolled.
    int cursor_x;
    int cursor_y;
    int num_lines;
    int need_scroll_update;
    float prev_cursor_x;
    float prev_cursor_y;
    int font_width;
    int font_height;
    char *last_compile_log;
    error_msg_t *error_msgs;
    STB_TexteditState state;
} EditorState;

float STB_TEXTEDIT_GETWIDTH(EditorState *obj, int n, int i) {
    char c = obj->str[i + n];
    if (c == '\t')
        return obj->font_width * 2;
    if (c == '\n' || c == 0)
        return STB_TEXTEDIT_GETWIDTH_NEWLINE;
    return obj->font_width;
}

int STB_TEXTEDIT_INSERTCHARS(EditorState *obj, int i, const char *c, int n) {
    stbds_arrinsn(obj->str, i, n);
    memcpy(obj->str + i, c, n);
    return 1;
}

static inline int get_select_start(EditorState *obj) { return mini(obj->state.select_end, obj->state.select_start); }
static inline int get_select_end(EditorState *obj) { return maxi(obj->state.select_end, obj->state.select_start); }

int STB_TEXTEDIT_KEYTOTEXT(int key) {
    if (key == '\n' || key == GLFW_KEY_ENTER)
        return '\n';
    if (key == '\t' || key == GLFW_KEY_TAB)
        return '\t';
    if (key < ' ' || key > 126)
        return -1;
    return key;
}
void STB_TEXTEDIT_LAYOUTROW(StbTexteditRow *r, EditorState *obj, int n) {
    r->x0 = 0;
    r->baseline_y_delta = obj->font_height;
    r->ymin = 0;
    r->ymax = obj->font_height;
    int len = stbds_arrlen(obj->str);
    r->num_chars = len - n;
    r->x1 = obj->font_width * r->num_chars;
    for (int i = n; i < len; ++i) {
        if (obj->str[i] == '\n' || obj->str[i] == 0) {
            r->num_chars = i - n + 1;
            r->x1 = obj->font_width * (r->num_chars - 1);
            break;
        }
    }
}
#define STB_TEXTEDIT_IMPLEMENTATION
#include "3rdparty/stb_textedit.h"
