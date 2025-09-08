// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE

#include "3rdparty/stb_image.h"

#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

int mini(int a, int b) { return a < b ? a : b; }
int maxi(int a, int b) { return a > b ? a : b; }

#define STB_DS_IMPLEMENTATION
#include "3rdparty/stb_ds.h"

typedef struct EditableString {
    char *str; // stb stretchy buffer
} EditableString;

#define FONT_WIDTH 8
#define FONT_HEIGHT 16

#define STB_TEXTEDIT_CHARTYPE char
#define STB_TEXTEDIT_POSITIONTYPE int
#define STB_TEXTEDIT_UNDOSTATECOUNT 99
#define STB_TEXTEDIT_UNDOCHARCOUNT 999
#define STB_TEXTEDIT_STRING EditableString
#define STB_TEXTEDIT_STRINGLEN(obj) stbds_arrlen(obj->str)
#define STB_TEXTEDIT_GETWIDTH_NEWLINE -1.f
float STB_TEXTEDIT_GETWIDTH(EditableString *obj, int n, int i) {
    char c = obj->str[i + n];
    if (c == '\t')
        return FONT_WIDTH * 4;
    if (c == '\n')
        return STB_TEXTEDIT_GETWIDTH_NEWLINE;
    return FONT_WIDTH;
}
#define STB_TEXTEDIT_NEWLINE '\n'
#define STB_TEXTEDIT_GETCHAR(obj, i) obj->str[i]
#define STB_TEXTEDIT_DELETECHARS(obj, i, n) stbds_arrdeln(obj->str, i, n)
#define STB_TEXTEDIT_INSERTCHARS(obj, i, c, n)                                                                                     \
    ({                                                                                                                             \
        stbds_arrinsn(obj->str, i, n);                                                                                             \
        memcpy(obj->str + i, c, n);                                                                                                \
        1;                                                                                                                         \
    })
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
#define STB_TEXTEDIT_K_WORDLEFT GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_WORDRIGHT GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
// #define STB_TEXTEDIT_K_LINESTART2 GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
// #define STB_TEXTEDIT_K_LINEEND2 GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_TEXTSTART2 (GLFW_KEY_UP | (GLFW_MOD_SUPER << 16))
#define STB_TEXTEDIT_K_TEXTEND2 (GLFW_KEY_DOWN | (GLFW_MOD_SUPER << 16))

static int char_is_separator(char c) {
    return c == ',' || c == '.' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '|' ||
           c == '!' || c == '\\' || c == '/' || c == '\n' || c == '\r' || c == '<' || c == '>' || c=='#' || c=='\'' || c=='"';
}
static int char_is_blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_word_boundary_from_right(EditableString *obj, int idx) {
    char curr_c = obj->str[idx];
    char prev_c = (idx > 0) ? obj->str[idx - 1] : '\0';
    bool prev_white = char_is_blank(prev_c);
    bool prev_separ = char_is_separator(prev_c);
    bool curr_white = char_is_blank(curr_c);
    bool curr_separ = char_is_separator(curr_c);
    return ((prev_white || prev_separ) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
}
static int is_word_boundary_from_left(EditableString *obj, int idx) {
    char curr_c = obj->str[idx];
    char prev_c = (idx > 0) ? obj->str[idx - 1] : '\0';
    bool prev_white = char_is_blank(prev_c);
    bool prev_separ = char_is_separator(prev_c);
    bool curr_white = char_is_blank(curr_c);
    bool curr_separ = char_is_separator(curr_c);
    return ((prev_white) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
}
static int stb_move_word_left(EditableString *obj, int idx) {
    idx--;
    while (idx >= 0 && !is_word_boundary_from_right(obj, idx))
        idx--;
    return idx < 0 ? 0 : idx;
}
static int stb_move_word_right(EditableString *obj, int idx) {
    int len = stbds_arrlen(obj->str);
    idx++;
    while (idx < len && !is_word_boundary_from_left(obj, idx))
        idx++;
    return idx > len ? len : idx;
}
#define STB_TEXTEDIT_MOVEWORDLEFT stb_move_word_left
#define STB_TEXTEDIT_MOVEWORDRIGHT stb_move_word_right

#include "3rdparty/stb_textedit.h"
int STB_TEXTEDIT_KEYTOTEXT(int key) {
    if (key == '\n' || key == GLFW_KEY_ENTER)
        return '\n';
    if (key == '\t' || key == GLFW_KEY_TAB)
        return '\t';
    if (key < ' ' || key > 126)
        return -1;
    return key;
}
void STB_TEXTEDIT_LAYOUTROW(StbTexteditRow *r, EditableString *obj, int n) {
    r->x0 = 0;
    r->baseline_y_delta = FONT_HEIGHT;
    r->ymin = 0;
    r->ymax = FONT_HEIGHT;
    int len = stbds_arrlen(obj->str);
    r->num_chars = len - n;
    r->x1 = FONT_WIDTH * r->num_chars;
    for (int i = n; i < len; ++i) {
        if (obj->str[i] == '\n') {
            r->num_chars = i - n + 1;
            r->x1 = FONT_WIDTH * (r->num_chars - 1);
            break;
        }
    }
}
#define STB_TEXTEDIT_IMPLEMENTATION
#include "3rdparty/stb_textedit.h"

static void die(const char *msg) {
    fprintf(stderr, "ERR: %s\n", msg);
    exit(1);
}
static void check_gl(const char *where) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "GL error 0x%04x at %s\n", e, where);
        exit(1);
    }
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, NULL, log);
        fprintf(stderr, "Shader compile failed:\n%s\n", log);
        free(log);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        char *log = (char *)malloc(len > 1 ? len : 1);
        glGetProgramInfoLog(p, len, NULL, log);
        fprintf(stderr, "Program link failed:\n%s\n", log);
        free(log);
        return 0;
    }
    return p;
}

#define SHADER(...) "#version 330 core\n" #__VA_ARGS__

//clang-format off
const char *kVS = SHADER(out vec2 v_uv; uniform ivec2 uScreenPx; void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p * vec2(uScreenPx);
    gl_Position = vec4(p.x * 2.0 - 1.0, p.y * -2.0 + 1.0, 0.0, 1.0);
});

const char *kFS = SHADER(out vec4 o_color; in vec2 v_uv; uniform sampler2D uFont; uniform usampler2D uText; void main() {
    ivec2 pixel = ivec2(v_uv);
    ivec2 cell = pixel / ivec2(8, 16); // FONT_WIDTH, FONT_HEIGHT
    ivec2 fontpix = ivec2(pixel - cell * ivec2(8, 16));
    uvec4 char_attrib = texelFetch(uText, cell, 0);
    int thechar = int(char_attrib.r) - 32;
    vec4 fg = vec4(0.0);
    vec4 bg = vec4(0.0);
    // bbbfffcc -> bb bf ff cc ( A B G R )
    fg.z = float(char_attrib.b & 15u) * (1.f / 15.f);
    fg.y = float(char_attrib.g >> 4u) * (1.f / 15.f);
    fg.x = float(char_attrib.g & 15u) * (1.f / 15.f);
    bg.z = float(char_attrib.b >> 4u) * (1.f / 15.f);
    bg.y = float(char_attrib.a >> 4u) * (1.f / 15.f);
    bg.x = float(char_attrib.a & 15u) * (1.f / 15.f);
    fontpix.x += (thechar & 15) * 8;
    fontpix.y += (thechar >> 4) * 16;
    float fontlvl = (thechar >= 0) ? texelFetch(uFont, fontpix, 0).r : 1.0;
    o_color = mix(fg, bg, fontlvl);
});
//clang-format on

EditableString edit_str = {};
STB_TexteditState state = {};
static void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_TAB)
            key = '\t';
        if (key == GLFW_KEY_ENTER)
            key = '\n';

        if (key < 32 || key > 126 || (mods & GLFW_MOD_SUPER)) {
            //printf("key: %d, mods: %d\n", key, mods);
            stb_textedit_key(&edit_str, &state, key | (mods << 16));
            //printf("cursor %d , select_start %d, select_end %d\n", state.cursor, state.select_start, state.select_end);
        }
    }
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    //printf("char: %d\n", codepoint);
    stb_textedit_key(&edit_str, &state, codepoint);
}

static void mouse_button_callback(GLFWwindow *win, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        stb_textedit_click(&edit_str, &state, mx, my);
    }
    if (action == GLFW_RELEASE) {
        printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        stb_textedit_drag(&edit_str, &state, mx, my);
    }
}

int main(int argc, char **argv) {
    int want_fullscreen = (argc > 1 && strcmp(argv[1], "--fullscreen") == 0);
    if (!glfwInit())
        die("glfwInit failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    GLFWmonitor *mon = want_fullscreen ? glfwGetPrimaryMonitor() : NULL;
    const GLFWvidmode *vm = want_fullscreen ? glfwGetVideoMode(mon) : NULL;
    int ww = want_fullscreen ? vm->width : 1280;
    int wh = want_fullscreen ? vm->height : 720;

    GLFWwindow *win = glfwCreateWindow(ww, wh, "ginkgo", mon, NULL);
    if (!win)
        die("glfwCreateWindow failed");
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFS);
    GLuint prog = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint loc_uText = glGetUniformLocation(prog, "uText");
    GLint loc_uFont = glGetUniformLocation(prog, "uFont");
    GLint loc_uTextSize = glGetUniformLocation(prog, "uTextSize");
    GLint loc_uCellPx = glGetUniformLocation(prog, "uCellPx");
    GLint loc_uScreenPx = glGetUniformLocation(prog, "uScreenPx");
    GLint loc_uFontPx = glGetUniformLocation(prog, "uFontPx");

    // Create textures
    GLuint texText = 0, texFont = 0;
    glGenTextures(1, &texText);
    glBindTexture(GL_TEXTURE_2D, texText);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 256, 256, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    check_gl("alloc text tex");

    int fw = 0, fh = 0, fc = 0;
    stbi_uc *fontPixels = stbi_load("scripts/font_256.png", &fw, &fh, &fc, 4);
    if (!fontPixels)
        die("Failed to load font");
    glGenTextures(1, &texFont);
    glBindTexture(GL_TEXTURE_2D, texFont);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, fontPixels);
    stbi_image_free(fontPixels);
    check_gl("upload font");

    size_t textBytes = (size_t)(256 * 256 * 4);
    GLuint pbos[3];
    glGenBuffers(3, pbos);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    int pbo_index = 1;

    FILE *f = fopen("gpu.c", "r");
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    stbds_arrsetlen(edit_str.str, len);
    fseek(f, 0, SEEK_SET);
    fread(edit_str.str, 1, len, f);
    fclose(f);

    stb_textedit_initialize_state(&state, 0);

    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        int m0 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int m1 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (m0) {
            stb_textedit_drag(&edit_str, &state, mx, my);
        }
        int kEsc = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (kEsc)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);

        double t = glfwGetTime() - t0;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[pbo_index]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
        uint32_t *ptr = (uint32_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, textBytes,
                                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (ptr) {
            memset(ptr, 0, textBytes);
            int x = 0, y = 0;
            int arrlen = stbds_arrlen(edit_str.str);
            for (int idx = 0; idx <= arrlen; ++idx) {
                char c = idx < arrlen ? edit_str.str[idx] : ' ';
                uint32_t col = 0x000fff00;
                int se = mini(state.select_start, state.select_end);
                int ee = maxi(state.select_start, state.select_end);
                if (idx >= se && idx < ee)
                    col = 0x48ffff00;
                if (idx == state.cursor)
                    col = 0xfff00000;
                if (x < 256)
                    ptr[y * 256 + x] = col | c;
                if (c == '\t')
                    x += 4;
                else if (c == '\n') {
                    x = 0;
                    y++;
                    if (y == 256)
                        break;
                } else
                    ++x;
            }
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        }
        glBindTexture(GL_TEXTURE_2D, texText);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, (const void *)0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        pbo_index = (pbo_index + 1) % 3;

        // --- Draw ---
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(prog);
        glUniform1i(loc_uText, 0);
        glUniform1i(loc_uFont, 1);
        glUniform2i(loc_uScreenPx, fbw, fbh);
        glUniform2i(loc_uFontPx, fw, fh);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texText);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texFont);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(win);
    }

    // Cleanup
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texText);
    glDeleteTextures(1, &texFont);
    glDeleteProgram(prog);
    glDeleteBuffers(3, pbos);
    // free(staging);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
