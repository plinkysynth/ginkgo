// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"

#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers

#define FONT_WIDTH 8
#define FONT_HEIGHT 16
// size of virtual textmode screen:
#define TMW 512
#define TMH 256

int mini(int a, int b) { return a < b ? a : b; }
int maxi(int a, int b) { return a > b ? a : b; }

typedef struct EditableString {
    char *str; // stb stretchy buffer
} EditableString;

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
const char *kVS = SHADER(out vec2 v_uv; out vec2 v_uvn; uniform ivec2 uScreenPx; void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uvn = p;
    v_uv = p * vec2(uScreenPx);
    gl_Position = vec4(p.x * 2.0 - 1.0, p.y * -2.0 + 1.0, 0.0, 1.0);
});

const char *kFS2 = SHADER(out vec4 o_color; in vec2 v_uvn; uniform sampler2D uFont; uniform usampler2D uText; void main() {
    vec2 p = v_uvn;
    o_color = vec4(p, 0.5, 1.0);
});

const char *kFS = SHADER(
out vec4 o_color; 
in vec2 v_uv; 
in vec2 v_uvn; 
uniform sampler2D uFP; 
uniform sampler2D uFont; 
uniform usampler2D uText; 
void main() {
    vec4 rendercol = texture(uFP, v_uvn);
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
    vec4 fontcol = mix(fg, bg, fontlvl);
    float alpha = (fontcol.x == 0. && fontcol.y == 0. && fontcol.z == 0.) ? 0.0 : 0.75;
    o_color = mix(rendercol, fontcol, alpha);
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

GLuint gl_create_texture(int filter_mode, int wrap_mode) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    return tex;
}

GLFWwindow *gl_init(int want_fullscreen) {
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

    return win;
}

int main(int argc, char **argv) {
    int want_fullscreen = (argc > 1 && strcmp(argv[1], "--fullscreen") == 0);
    GLFWwindow *win = gl_init(want_fullscreen);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFS);
    GLuint fs2 = compile_shader(GL_FRAGMENT_SHADER, kFS2);
    GLuint prog = link_program(vs, fs);
    GLuint prog2 = link_program(vs, fs2);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteShader(fs2);

    GLint loc_uText = glGetUniformLocation(prog, "uText");
    GLint loc_uFont = glGetUniformLocation(prog, "uFont");
    GLint loc_uFP = glGetUniformLocation(prog, "uFP");
    GLint loc_uTextSize = glGetUniformLocation(prog, "uTextSize");
    GLint loc_uCellPx = glGetUniformLocation(prog, "uCellPx");
    GLint loc_uScreenPx = glGetUniformLocation(prog, "uScreenPx");
    GLint loc_uFontPx = glGetUniformLocation(prog, "uFontPx");

    int fw = 0, fh = 0, fc = 0;
    stbi_uc *fontPixels = stbi_load("assets/font.png", &fw, &fh, &fc, 4);
    if (!fontPixels)
        die("Failed to load font");
    GLuint texFont = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, fontPixels);
    stbi_image_free(fontPixels);
    check_gl("upload font");
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint texText = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, TMW, TMH, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    check_gl("alloc text tex");
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint texFPRT = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 2560, 1440, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    check_gl("alloc fpRT");
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texFPRT, 0);
    GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, bufs);
    GLenum fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbo_status != GL_FRAMEBUFFER_COMPLETE) 
        die("fbo is not complete");
    check_gl("alloc fbo");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    
    size_t textBytes = (size_t)(TMW * TMH * 4);
    GLuint pbos[3];
    int pbo_index = 0;
    glGenBuffers(3, pbos);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

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
        int tmw = fbw/FONT_WIDTH;
        int tmh = fbh/FONT_HEIGHT;

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
                if (x < TMW)
                    ptr[y * TMW + x] = col | c;
                if (c == '\t')
                    x += 4;
                else if (c == '\n') {
                    x = 0;
                    y++;
                    if (y == TMH)
                        break;
                } else
                    ++x;
            }
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        }
        glBindTexture(GL_TEXTURE_2D, texText);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TMW, TMH, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, (const void *)0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        pbo_index = (pbo_index + 1) % 3;
        check_gl("update text tex");

        glDisable(GL_DEPTH_TEST);

        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        check_gl("bind fbo");
        glViewport(0, 0, 2560, 1440);
        //glClearColor(1.f, 0.f, 1.f, 1.f);
        //glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog2); // a fragment shader that writes RGBA16F
        check_gl("use prog2");
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        check_gl("draw fpRT");

        //glClearColor(0.f, 0.f, 0.f, 1.f);
        //glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glUseProgram(prog);

        glUniform1i(loc_uText, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texText);

        glUniform1i(loc_uFont, 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texFont);

        glUniform1i(loc_uFP, 2);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texFPRT);

        glUniform2i(loc_uScreenPx, fbw, fbh);
        glUniform2i(loc_uFontPx, fw, fh);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        check_gl("draw text");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);

        glfwSwapBuffers(win);
    }

    // Cleanup
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texText);
    glDeleteTextures(1, &texFont);
    glDeleteTextures(1, &texFPRT);
    glDeleteProgram(prog);
    glDeleteBuffers(3, pbos);
    // free(staging);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
