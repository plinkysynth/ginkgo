// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#define MINIAUDIO_IMPLEMENTATION
#define PFFFT_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "3rdparty/pffft.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers

#define RESW 1920
#define RESH 1080
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
// size of virtual textmode screen:
#define TMW 512
#define TMH 256

#define SAMPLE_RATE 48000
#define SCOPE_SIZE 65536
#define FFT_SIZE 8192
#define SCOPE_MASK (SCOPE_SIZE-1)

#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f

int mini(int a, int b) { return a < b ? a : b; }
int maxi(int a, int b) { return a > b ? a : b; }
int clampi(int a, int min, int max) { return a < min ? min : a > max ? max : a; }
uint32_t minu(uint32_t a, uint32_t b) { return a < b ? a : b; }
uint32_t maxu(uint32_t a, uint32_t b) { return a > b ? a : b; }
uint32_t clampu(uint32_t a, uint32_t min, uint32_t max) { return a < min ? min : a > max ? max : a; }
float minf(float a, float b) { return a < b ? a : b; }
float maxf(float a, float b) { return a > b ? a : b; }
float clampf(float a, float min, float max) { return a < min ? min : a > max ? max : a; }
float squaref(float x) { return x * x; }
float lin2db(float x) { return 8.6858896381f * logf(maxf(1e-20f, x)); }
float db2lin(float x) { return expf(x / 8.6858896381f); }
float squared2db(float x) { return 4.342944819f * logf(maxf(1e-20f, x)); }
float db2squared(float x) { return expf(x / 4.342944819f); }

float scope[SCOPE_SIZE][2];
uint32_t scope_pos = 0;
static void audio_cb(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    float *o = (float *)out;
    const float *i = (const float *)in;
    for (ma_uint32 k = 0; k < frames * 2; k++) 
        o[k] = 0.f;
    static uint32_t sampleidx = 0;
    for (ma_uint32 k = 0; k < frames; ++k) {
        float iTime = sampleidx * (1.f/SAMPLE_RATE);
        float x = sampleidx * 0.05f;
        float l = tanhf((2.+sinf(iTime*-10.35+x*0.031))*sin(x*0.01253+iTime)*sin(x*0.2));
        float r = l;
        l=clampf(l,-1.f,1.f);
        r=clampf(r,-1.f,1.f);
        o[k*2] = l;
        o[k*2+1] = r;
        scope[scope_pos & SCOPE_MASK][0] = l;
        scope[scope_pos & SCOPE_MASK][1] = r;
        scope_pos++;
        sampleidx++;
    }
}

typedef struct EditableString {
    char *str; // stb stretchy buffer
    float scroll_y;
    float scroll_y_target;
    int intscroll; // how many lines we scrolled.
    int cursor_x;
    int cursor_y;
    int num_lines;
    int need_scroll_update;
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
const char *kVS = SHADER(
out vec2 v_uv; 
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p;
    gl_Position = vec4(p.x * 2.0 - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0);
});

const char *kFS2 = SHADER(
out vec4 o_color; 
in vec2 v_uv;
uniform uint iFrame;
uint seed;	
uniform sampler2D uFP;
uniform ivec2 uScreenPx; 
uint pcg_next() {
  return seed = seed * 747796405u + 2891336453u;
}
float rnd() { return float((pcg_next())) / 4294967296.0; }
vec2 rnd2() { return vec2(rnd(),rnd()); }
vec2 rnd_disc_cauchy() {
    vec2 h=rnd2() * vec2(6.28318530718,3.1/2.);
    h.y=tan(h.y);
    return h.y*vec2(sin(h.x),cos(h.x));
}
void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    seed = uint(uint(fragCoord.x) * uint(1973) + uint(fragCoord.y) * uint(9277) + uint(iFrame) * uint(23952683)) | uint(1);   
    vec2 uv = fragCoord;
    o_color = vec4(uv ,0.0,1.0);
});

const char *kFS = SHADER(
uniform ivec2 uScreenPx; 
uniform float iTime;
uniform float scroll_y;
out vec4 o_color; 
in vec2 v_uv; 
uniform sampler2D uFP; 
uniform sampler2D uFont; 
uniform usampler2D uText; 
vec3 filmicToneMapping(vec3 color)
{
	color = max(vec3(0.), color - vec3(0.004));
	color = (color * (6.2 * color + .5)) / (color * (6.2 * color + 1.7) + 0.06);
	return color;
}
vec2 scope(float x) {
    int ix=int(x);
    // TMH
    uvec4 s0 = texelFetch(uText, ivec2(ix& 511, (256-6) + (ix>>9)) , 0);
    return (vec2(s0.rg)-128.f)*0.25; 
}
vec2 scope_beam(vec2 pix) {
    pix.x-=32.;
    vec2 prevy = scope(pix.y-0.33);
    vec2 nexty = scope(pix.y+0.33);
    vec2 midy=(prevy+nexty)*0.5-pix.x;
    vec2 deltay=abs(prevy-nexty)+0.1;
    return (0.25/deltay)*exp2(-2.*(((midy*midy)/deltay)));
}
void main() {
    vec3 rendercol ;//= texture(uFP, v_uv).rgb;
    // float kernel_size = 0.25f; // the gap is about double this.
    // float dx = kernel_size * (16./9.) * (1./1280.);
    // float dy = kernel_size * (1./720.);
    // rendercol += texture(uFP, v_uv + vec2(dx, dy)).rgb;
    // rendercol += texture(uFP, v_uv - vec2(dx, dy)).rgb;
    // rendercol += texture(uFP, v_uv + vec2(-dx, dy)).rgb;
    // rendercol += texture(uFP, v_uv - vec2(-dx, dy)).rgb;
    // rendercol *= 0.2;
    
    vec2 pix = v_uv * uScreenPx;
    vec2 beam = scope_beam(pix) + scope_beam(pix+vec2(0.,0.5));
    rendercol = sqrt(beam.x*vec3(0.2,0.4,0.8) + beam.y*vec3(0.8,0.4,0.2));    
    rendercol.rgb = sqrt(rendercol.rgb); // filmicToneMapping(rendercol.rgb);
    
    
    ivec2 pixel = ivec2(v_uv.x * uScreenPx.x, (1.0 - v_uv.y) * uScreenPx.y + scroll_y);
    ivec2 cell = pixel / ivec2(8, 16); // FONT_WIDTH, FONT_HEIGHT
    ivec2 fontpix = ivec2(pixel - cell * ivec2(8, 16));
    uvec4 char_attrib = texelFetch(uText, cell, 0);
    int thechar = int(char_attrib.r) - 32;
    vec3 fg = vec3(0.0);
    vec3 bg = vec3(0.0);
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
    vec3 fontcol = mix(fg, bg, fontlvl);
    float alpha = (fontcol.x == 0. && fontcol.y == 0. && fontcol.z == 0.) ? 0.0 : 0.75;
    o_color = vec4(mix(rendercol, fontcol.rgb, alpha), 1.0);
});
//clang-format on

EditableString edit_str = {};
STB_TexteditState state = {};

int fbw, fbh; // current framebuffer size in pixels
int tmw, tmh; // current textmap size in cells

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
            edit_str.need_scroll_update = true;
        }
    }
}

static void scroll_callback(GLFWwindow *win, double xoffset, double yoffset) {
    //printf("scroll: %f\n", yoffset);
    edit_str.scroll_y_target -= yoffset * FONT_HEIGHT;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    //printf("char: %d\n", codepoint);
    stb_textedit_key(&edit_str, &state, codepoint);
    edit_str.need_scroll_update = true;

}

static void mouse_button_callback(GLFWwindow *win, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        stb_textedit_click(&edit_str, &state, mx - 64., my + edit_str.scroll_y);
        edit_str.need_scroll_update = true;

    }
    if (action == GLFW_RELEASE) {
        printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        stb_textedit_drag(&edit_str, &state, mx - 64., my + edit_str.scroll_y);
        edit_str.need_scroll_update = true;

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
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, !want_fullscreen);
#endif



    GLFWmonitor *mon = want_fullscreen ? glfwGetPrimaryMonitor() : NULL;
    // int count; 
    // const GLFWvidmode* modes = glfwGetVideoModes(mon, &count);
    // const GLFWvidmode* pick = NULL;
    // for (int i=0;i<count;i++) {
    //     printf("mode %d: %d x %d - %d Hz\n", i, modes[i].width, modes[i].height, modes[i].refreshRate);
    // }
    const GLFWvidmode *vm = want_fullscreen ? glfwGetVideoMode(mon) : NULL;
    int ww = want_fullscreen ? vm->width : 1920/2;
    int wh = want_fullscreen ? vm->height : 1200/2;

    GLFWwindow *win = glfwCreateWindow(ww, wh, "ginkgo", mon, NULL);
    if (!win)
        die("glfwCreateWindow failed");
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    return win;
}

int main(int argc, char **argv) {

    printf("ginkgo - " __DATE__ " " __TIME__ "\n");

    PFFFT_Setup *fft_setup = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
    float *fft_work = (float *)pffft_aligned_malloc(FFT_SIZE * 2 * sizeof(float));
    float *fft_window = (float *)pffft_aligned_malloc(FFT_SIZE * sizeof(float));
    for (int i = 0; i < FFT_SIZE; ++i) {
        fft_window[i] = 0.5f - 0.5f * cosf(2.f * M_PI * i / FFT_SIZE);
    }
    printf("fft simd size: %d\n", pffft_simd_size());

    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = SAMPLE_RATE;
    cfg.capture.format = cfg.playback.format = ma_format_f32;
    cfg.capture.channels = cfg.playback.channels = 2;
    cfg.dataCallback = audio_cb;
    ma_device dev;
    if (ma_device_init(NULL, &cfg, &dev) != MA_SUCCESS) {
        die("ma_device_init failed");
    }
    if (ma_device_start(&dev) != MA_SUCCESS) {
        fprintf(stderr, "ma_device_start failed\n");
        ma_device_uninit(&dev);
        return 3;
    }


    int want_fullscreen = (argc > 1 && (strcmp(argv[1], "--fullscreen") == 0 || strcmp(argv[1], "-f") == 0));
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
    GLint loc_iTime = glGetUniformLocation(prog, "iTime");
    GLint loc_scroll_y = glGetUniformLocation(prog, "scroll_y");
    GLint loc_uTextSize = glGetUniformLocation(prog, "uTextSize");
    GLint loc_uCellPx = glGetUniformLocation(prog, "uCellPx");
    GLint loc_uScreenPx = glGetUniformLocation(prog, "uScreenPx");
    GLint loc_uScreenPx2 = glGetUniformLocation(prog2, "uScreenPx");
    GLint loc_iFrame = glGetUniformLocation(prog2, "iFrame");
    GLint loc_uFP2 = glGetUniformLocation(prog2, "uFP");
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

    GLuint texFPRT[2];
    GLuint fbo[2];
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &fbo[i]);
        texFPRT[i] = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, texFPRT[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, RESW, RESH, 0, GL_RGBA, GL_FLOAT, NULL);
        check_gl("alloc fpRT");
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texFPRT[i], 0);
        GLenum bufs[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, bufs);
        GLenum fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbo_status != GL_FRAMEBUFFER_COMPLETE) 
            die("fbo is not complete");
        check_gl("alloc fbo");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
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

    FILE *f = fopen("ginkgo.c", "r");
    assert(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    stbds_arrsetlen(edit_str.str, len);
    fseek(f, 0, SEEK_SET);
    fread(edit_str.str, 1, len, f);
    fclose(f);
    stb_textedit_initialize_state(&state, 0);
    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        int m0 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int m1 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (m0) {
            stb_textedit_drag(&edit_str, &state, mx - 64., my + edit_str.scroll_y);
            edit_str.need_scroll_update = true;

        }
        int kEsc = glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (kEsc)
            glfwSetWindowShouldClose(win, GLFW_TRUE);

        glfwGetFramebufferSize(win, &fbw, &fbh);
        //printf("fbw: %d, fbh: %d\n", fbw, fbh);
        tmw = fbw/FONT_WIDTH;
        tmh = fbh/FONT_HEIGHT;
        if (tmw>512) tmw=512;
        if (tmh>256-8) tmh=256-8;

        double t = glfwGetTime() - t0;
        edit_str.scroll_y += (edit_str.scroll_y_target - edit_str.scroll_y) * 0.1;
        if (edit_str.scroll_y < 0)
            edit_str.scroll_y = 0;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[pbo_index]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
        uint32_t *ptr = (uint32_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, textBytes,
                                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (ptr) {
            memset(ptr, 0, textBytes);
            edit_str.intscroll = (int)(edit_str.scroll_y / FONT_HEIGHT); 
            int x = 8, y = -edit_str.intscroll;
            int arrlen = stbds_arrlen(edit_str.str);
            for (int idx = 0; idx <= arrlen; ++idx) {
                char c = idx < arrlen ? edit_str.str[idx] : ' ';
                uint32_t col = 0x000fff00;
                int se = mini(state.select_start, state.select_end);
                int ee = maxi(state.select_start, state.select_end);
                if (idx >= se && idx < ee)
                    col = 0x48ffff00;
                if (idx == state.cursor) {
                     col = 0xfff00000;
                     edit_str.cursor_x = x;
                     edit_str.cursor_y = y + edit_str.intscroll;
                }
                if (x < tmw && y>=0 && y<tmh)
                    ptr[y * TMW + x] = col | c;
                if (c == '\t')
                    x += 4;
                else if (c == '\n') {
                    x = 8;
                    y++;
                } else
                    ++x;
            }
            edit_str.num_lines = y + 1 + edit_str.intscroll;
            if (edit_str.need_scroll_update) {
                edit_str.need_scroll_update = false;
                if (edit_str.cursor_y >= edit_str.intscroll + tmh - 4) {
                    edit_str.scroll_y_target = (edit_str.cursor_y - tmh + 4) * FONT_HEIGHT;
                } else if (edit_str.cursor_y < edit_str.intscroll + 4) {
                    edit_str.scroll_y_target = (edit_str.cursor_y - 4) * FONT_HEIGHT;
                }
            }
            edit_str.scroll_y_target = clampf(edit_str.scroll_y_target, 0, (edit_str.num_lines-tmh+4) * FONT_HEIGHT);
            // now find a zero crossing in the scope, and copy the relevant section 
            // scope cant be more than 2048 as that's how many slots we have in the texture.
            uint32_t scope_start = scope_pos - 1024;
            uint32_t scan_max = 1024;
            int bestscani = 0;
            float bestscan = 0.f;
            for (int i=1;i<scan_max;++i) {
                float mono = scope[(scope_start - i) & SCOPE_MASK][0] + scope[(scope_start - i) & SCOPE_MASK][1];
                float mono_next = scope[(scope_start - i + 1) & SCOPE_MASK][0] + scope[(scope_start - i + 1) & SCOPE_MASK][1];
                float delta = mono_next - mono;
                if (mono < 0.f && mono_next > 0.f && delta > bestscan) {
                    bestscan = delta;
                    bestscani = i;
                    break;
                }
            }
            scope_start -= 1024 + bestscani;
            uint32_t *scope_dst = ptr + (TMH-4) * TMW;
            for (int i=0;i<2048;++i) {
                float l = scope[(scope_start + i) & SCOPE_MASK][0];
                float r = scope[(scope_start + i) & SCOPE_MASK][1];
                l = clampf(l, -1.f, 1.f);
                r = clampf(r, -1.f, 1.f);
                uint8_t l8 = (uint8_t)(l * 127.f + 128.f);
                uint8_t r8 = (uint8_t)(r * 127.f + 128.f);
                scope_dst[i] = (l8 << 0) | (r8 << 8);
            }

            uint32_t fft_start = scope_pos - FFT_SIZE;
            float fft_buf[2][FFT_SIZE];
            for (int i=0;i<FFT_SIZE;++i) {
                float l = scope[(fft_start - i - 1) & SCOPE_MASK][0];
                float r = scope[(fft_start - i - 1) & SCOPE_MASK][1];
                fft_buf[0][i] = l * fft_window[i];
                fft_buf[1][i] = r * fft_window[i];
            }
            pffft_transform_ordered(fft_setup, fft_buf[0], fft_buf[0], fft_work, PFFFT_FORWARD);
            pffft_transform_ordered(fft_setup, fft_buf[1], fft_buf[1], fft_work, PFFFT_FORWARD);
            scope_dst = ptr + (TMH-6) * TMW;
            float peak_mag = squared2db(squaref(0.25f*FFT_SIZE)); // assuming hann, coherent gain is 0.5
            // int mini=0, maxi=0;
            // float minv=1000.f, maxv=-1000.f;
            for (int i=0;i<1024;++i) {
                float magl_db = squared2db(fft_buf[0][i*2] * fft_buf[0][i*2] + fft_buf[0][i*2+1] * fft_buf[0][i*2+1]) - peak_mag;
                float magr_db = squared2db(fft_buf[1][i*2] * fft_buf[1][i*2] + fft_buf[1][i*2+1] * fft_buf[1][i*2+1]) - peak_mag;
                // if (magl_db < minv) {minv = magl_db;mini = i;}
                // if (magl_db > maxv) {maxv = magl_db;maxi = i;}
                uint8_t l8 = (uint8_t)(clampf(255.f + magl_db * 4.f, 0.f, 255.f));
                uint8_t r8 = (uint8_t)(clampf(255.f + magr_db * 4.f, 0.f, 255.f));
                scope_dst[i] = (l8 << 0) | (r8 << 8);
            }
            //printf("fft min: %f in bin %d, max: %f in bin %d\n", minv, mini, maxv, maxi);

        
            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        }

        glBindTexture(GL_TEXTURE_2D, texText);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TMW, TMH, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, (const void *)0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        pbo_index = (pbo_index + 1) % 3;
        check_gl("update text tex");

        glDisable(GL_DEPTH_TEST);

        glDisable(GL_DEPTH_TEST);
        static uint32_t iFrame = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[iFrame % 2]);
        check_gl("bind fbo");
        glViewport(0, 0, RESW, RESH);
        //glClearColor((iFrame&1), 0.f, (iFrame&1)^1, 1.f);
        //glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog2); // a fragment shader that writes RGBA16F
        check_gl("use prog2");
        glUniform2i(loc_uScreenPx2, RESW, RESH);
        check_gl("uniform uScreenPx2");
        glUniform1ui(loc_iFrame, iFrame);
        check_gl("uniform iFrame");

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texFPRT[(iFrame + 1) % 2]);
        glUniform1i(loc_uFP2, 0); 
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
        glBindTexture(GL_TEXTURE_2D, texFPRT[iFrame % 2]);

        glUniform2i(loc_uScreenPx, fbw, fbh);
        glUniform2i(loc_uFontPx, fw, fh);
        glUniform1f(loc_iTime, t);
        glUniform1f(loc_scroll_y, edit_str.scroll_y - edit_str.intscroll * FONT_HEIGHT);
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
        iFrame++;
        
    }
    ma_device_stop(&dev);
    ma_device_uninit(&dev);

    // Cleanup
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texText);
    glDeleteTextures(1, &texFont);
    glDeleteTextures(2, texFPRT);
    glDeleteFramebuffers(2, fbo);
    glDeleteProgram(prog);
    glDeleteBuffers(3, pbos);
    // free(staging);

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
