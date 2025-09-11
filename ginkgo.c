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
#include <assert.h>
#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "3rdparty/pffft.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers

#define RESW 1920
#define RESH 1080
// size of virtual textmode screen:
#define TMW 512
#define TMH 256

#define OVERSAMPLE 2
#define SAMPLE_RATE_OUTPUT 48000
#define SAMPLE_RATE (SAMPLE_RATE_OUTPUT * OVERSAMPLE)
#define SCOPE_SIZE 65536
#define FFT_SIZE 8192
#define SCOPE_MASK (SCOPE_SIZE - 1)

#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f

#define countof(array) (sizeof(array) / sizeof(array[0]))

static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int a, int min, int max) { return a < min ? min : a > max ? max : a; }
static inline uint32_t minu(uint32_t a, uint32_t b) { return a < b ? a : b; }
static inline uint32_t maxu(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline uint32_t clampu(uint32_t a, uint32_t min, uint32_t max) { return a < min ? min : a > max ? max : a; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float clampf(float a, float min, float max) { return a < min ? min : a > max ? max : a; }
static inline float squaref(float x) { return x * x; }
static inline float lin2db(float x) { return 8.6858896381f * logf(maxf(1e-20f, x)); }
static inline float db2lin(float x) { return expf(x / 8.6858896381f); }
static inline float squared2db(float x) { return 4.342944819f * logf(maxf(1e-20f, x)); }
static inline float db2squared(float x) { return expf(x / 4.342944819f); }

typedef struct stereo {
    float l, r;
} stereo;

static inline float mid(stereo s) { return (s.l + s.r) * 0.5f; }
static inline float side(stereo s) { return (s.l - s.r) * 0.5f; }
static inline stereo saturate_stereo(stereo s) {
    // atanf is softer
    // return (stereo){.l = atanf(s.l) * (2.f/PI),.r = atanf(s.r) * (2.f/PI)};
    // tanhf is a little harder
    return (stereo){.l = tanhf(s.l), .r = tanhf(s.r)};
}

typedef struct error_msg_t {
    int key;           // a line number
    const char *value; // a line of text (terminated by \n)
} error_msg_t;

char *last_compile_log = NULL;
error_msg_t *error_msgs = NULL;
char status_bar[512];
double status_bar_time = 0;
uint32_t status_bar_color = 0;

static inline int is_ident_start(unsigned c) { return (c == '_') || ((c | 32) - 'a' < 26); }
static inline int is_ident_cont(unsigned c) { return is_ident_start(c) || (c - '0' < 10); }
static inline int is_space(unsigned c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }


// --- colors 3 digits bg, 3 digits fg
#define C_SELECTION 0xc48fffu

#define C_NUM 0x000cc7u
#define C_DEF 0x000fffu
#define C_KW 0x0009acu
#define C_TYPE 0x000a9cu
#define C_PREPROC 0x0009a9u
#define C_STR 0x000886u
#define C_COM 0x000fd8u
#define C_PUN 0x000aaau
#define C_ERR 0xf00fffu
#define C_OK 0x080fffu
#define C_WARNING 0x860fffu

void set_status_bar(uint32_t color, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vsnprintf(status_bar, sizeof(status_bar)-1, msg, args);
    va_end(args);
    fprintf(stderr, "%s\n", status_bar);
    status_bar_color = color;
    status_bar_time = glfwGetTime();
}


stereo scope[SCOPE_SIZE];
uint32_t scope_pos = 0;
static inline stereo compute_sample(uint32_t sample_idx) {
    float iTime = sample_idx * (1.f / SAMPLE_RATE);
    float x = sample_idx * 0.05f;
    float l = 0.f;
    float r = 0.f;
    for (int harm = 1; harm < 6; ++harm) {
        l += 2.f * sinf((sample_idx)*TAU * harm / 1512.f) /
             (harm); // tanhf((2.+sinf(iTime*-10.35+x*0.031))*sin(x*0.01253+iTime)*sin(x*0.2));
        r += 2.f * cosf((sample_idx)*TAU * harm / 1512.f) /
             (harm); // tanhf((2.+sinf(iTime*-10.35+x*0.031))*sin(x*0.01253+iTime)*sin(x*0.2));
    }
    float env = exp2f(-((sample_idx & 65535) / 2048.f));
    l *= env;
    r *= env;
    // l=(sample_idx&255) ? 0.f : 1.f;
    // r=l;
    return (stereo){.l = l, .r = r};
}
static void audio_cb(ma_device *d, void *out, const void *in, ma_uint32 frames) {
    float *o = (float *)out;
    const float *i = (const float *)in;
    for (ma_uint32 k = 0; k < frames * 2; k++)
        o[k] = 0.f;
    static uint32_t sampleidx = 0;
    static_assert(OVERSAMPLE == 2, "OVERSAMPLE must be 2");

#define K 16 // the kernel has this many non-center non-zero taps.
    // example with K=3:
    // x . x . x 0.5 x . x . x <- x = non zero taps
    //                         ^- history_pos
    const float fir_center_tap = 0.5000461f;
    const static float fir_kernel[K] = {
        0.3171385f, -0.1026337f, 0.0580279f, -0.0378839f, 0.0260971f, -0.0182989f, 0.0128162f, -0.0088559f,
        0.0059815f, -0.0039141f, 0.0024594f, -0.0014665f, 0.0008178f, -0.0004156f, 0.0001849f, -0.0000690f,
    };
    static stereo history[64];
    static_assert(countof(history) >= (K * 4 - 1), "history too small");
    static uint32_t history_pos = 0;
    for (ma_uint32 k = 0; k < frames; ++k) {
        // saturation on output...
        history[history_pos & 63] = saturate_stereo(compute_sample(history_pos));
        history[(history_pos + 1) & 63] = saturate_stereo(compute_sample(history_pos + 1));
        history_pos += 2;
        // 2x downsample FIR
        int center_idx = history_pos - K * 2;
        stereo acc = history[center_idx & 63]; // center tap
        acc.l *= fir_center_tap;
        acc.r *= fir_center_tap;
        for (int tap = 0; tap < K; ++tap) {
            stereo t0 = history[(center_idx + tap * 2 + 1) & 63];
            stereo t1 = history[(center_idx - tap * 2 - 1) & 63];
            acc.l += fir_kernel[tap] * (t0.l + t1.l);
            acc.r += fir_kernel[tap] * (t0.r + t1.r);
        }
        o[k * 2] = acc.l;
        o[k * 2 + 1] = acc.r;
        scope[scope_pos & SCOPE_MASK] = acc;
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
    float prev_cursor_x;
    float prev_cursor_y;
    int font_width;
    int font_height;
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
        return obj->font_width * 2;
    if (c == '\n' || c==0)
        return STB_TEXTEDIT_GETWIDTH_NEWLINE;
    return obj->font_width;
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
//#define STB_TEXTEDIT_K_WORDLEFT GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
//#define STB_TEXTEDIT_K_WORDRIGHT GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_LINESTART2 GLFW_KEY_LEFT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_LINEEND2 GLFW_KEY_RIGHT | (GLFW_MOD_SUPER << 16)
#define STB_TEXTEDIT_K_TEXTSTART2 (GLFW_KEY_UP | (GLFW_MOD_SUPER << 16))
#define STB_TEXTEDIT_K_TEXTEND2 (GLFW_KEY_DOWN | (GLFW_MOD_SUPER << 16))

static int char_is_separator(char c) {
    return c == ',' || c == '.' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '|' ||
           c == '!' || c == '\\' || c == '/' || c == '\n' || c == '\r' || c == '<' || c == '>' || c == '#' || c == '\'' || c == '"';
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
// #define STB_TEXTEDIT_MOVEWORDLEFT stb_move_word_left
// #define STB_TEXTEDIT_MOVEWORDRIGHT stb_move_word_right

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
    free(last_compile_log);
    last_compile_log = NULL;
    hmfree(error_msgs);
    error_msgs = NULL;
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        last_compile_log = (char *)malloc(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, NULL, last_compile_log);
        fprintf(stderr, "Shader compile failed:\n[%s]\n", last_compile_log);
        for (const char *c = last_compile_log; *c; c++) {
            int col = 0, line = 0;
            if (sscanf(c, "ERROR: %d:%d:", &col, &line) == 2) {
                hmput(error_msgs, line - 1, c);
            }
            while (*c && *c != '\n')
                c++;
        }
        set_status_bar(C_ERR, "Shader compile failed - %d errors", (int)hmlen(error_msgs));
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
#define SHADER_NO_VERSION(...) #__VA_ARGS__

// clang-format off
const char *kVS = SHADER(
out vec2 v_uv; 
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p;
    gl_Position = vec4(p.x * 2.0 - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0);
});

const char *kFS2_prefix = SHADER(
out vec4 o_color; 
in vec2 v_uv; 
uniform uint iFrame;
uniform float iTime; 
uint seed; 
uniform sampler2D uFP; 
uniform ivec2 uScreenPx;
float saturate(float x) { return clamp(x, 0.0, 1.0); }
uint pcg_next() { return seed = seed * 747796405u + 2891336453u; } 
float rnd() { return float((pcg_next())) / 4294967296.0; } 
vec2 rnd2() { return vec2(rnd(), rnd()); } 
vec3 rnd3() { return vec3(rnd(), rnd(), rnd()); } 
vec2 rnd_disc_cauchy() {
    vec2 h = rnd2() * vec2(6.28318530718, 3.1 / 2.);
    h.y = tan(h.y);
    return h.y * vec2(sin(h.x), cos(h.x));
}
vec3 c(vec2 uv) { 
    vec3 o=vec3(0.0); 
    float t = iTime;
    // user code follows
);
    
const char *kFS2_suffix = SHADER_NO_VERSION(
   return o; } // end of user shader
    void main() {
        vec2 fragCoord = gl_FragCoord.xy;
        seed = uint(uint(fragCoord.x) * uint(1973) + uint(fragCoord.y) * uint(9277) + uint(iFrame) * uint(23952683)) | uint(1);
        o_color = vec4(c(v_uv), 1.0);
    });

const char *kFS = SHADER(
    float saturate(float x) { return clamp(x, 0.0, 1.0); }
    uniform ivec2 uScreenPx; uniform float iTime; uniform float scroll_y; uniform vec4 cursor; out vec4 o_color; in vec2 v_uv;
    uniform ivec2 uFontPx;
    uniform int status_bar_size;
    uniform sampler2D uFP; uniform sampler2D uFont; uniform usampler2D uText; vec3 filmicToneMapping(vec3 color) {
        color = max(vec3(0.), color - vec3(0.004));
        color = (color * (6.2 * color + .5)) / (color * (6.2 * color + 1.7) + 0.06);
        return color;
    } vec2 wave_read(float x, int y_base) {
        int ix = int(x);
        vec2 s0 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
        ix++;
        vec2 s1 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
        return mix(s0, s1, fract(x));
    }

    vec2 scope(float x) { return (wave_read(x, 256 - 4) - 128.f) * 0.25; }

    void main() {
        vec3 rendercol= texture(uFP, v_uv).rgb;
        // float kernel_size = 0.25f; // the gap is about double this.
        // float dx = kernel_size * (16./9.) * (1./1280.);
        // float dy = kernel_size * (1./720.);
        // rendercol += texture(uFP, v_uv + vec2(dx, dy)).rgb;
        // rendercol += texture(uFP, v_uv - vec2(dx, dy)).rgb;
        // rendercol += texture(uFP, v_uv + vec2(-dx, dy)).rgb;
        // rendercol += texture(uFP, v_uv - vec2(-dx, dy)).rgb;
        // rendercol *= 0.2;

        vec2 pix = v_uv * vec2(uScreenPx.x, 2048.f);
        float fftx = uScreenPx.x - pix.x;
        bool is_fft = false;
        float ampscale = 0.25f;
        int base_y = 256 - 4;
        float nextx = pix.y + 1.;
        if (fftx < 128.f) {
            ampscale = 0.5f;
            is_fft = true;
            pix.y = (48.f / 48000.f * 8192.f) * pow(500.f, v_uv.y); // 24hz to nyquist
            base_y = 256 - 12;
            pix.x = fftx;
            nextx = pix.y * 1.003;
        }
        vec2 prevy = wave_read(pix.y, base_y) * ampscale;
        vec2 nexty = wave_read(nextx, base_y) * ampscale;
        vec2 miny = min(prevy, nexty);
        vec2 maxy = max(prevy, nexty);
        vec2 beam = 1.f - smoothstep(miny - 0.5f, maxy + 1.f, pix.xx);
        if (is_fft) {
            beam *= (100.f + fftx * fftx) * (0.5f / 16384.f);
        } else {
            beam *= smoothstep(miny - 2.f, maxy - 0.5f, pix.xx);
            beam *= 1.5f / (maxy - miny + 1.f);
        }
        rendercol = max(vec3(0.), rendercol);
        vec3 beamcol = vec3(0.2, 0.4, 0.8) * beam.x + vec3(0.8, 0.4, 0.2) * beam.y;
        float grey = dot(rendercol, vec3(0.2126, 0.7152, 0.0722));
        float cursor_col = 1.;
        if (grey > 0.5) {
            cursor_col = 0.;
            beamcol = -beamcol * 2.;
        }
        rendercol += beamcol;
        rendercol.rgb = sqrt(rendercol.rgb); // filmicToneMapping(rendercol.rgb);

        vec2 fpixel = vec2(v_uv.x * uScreenPx.x, (1.0 - v_uv.y) * uScreenPx.y);
        // status line doesnt scroll
        if (fpixel.y < uScreenPx.y - uFontPx.y * status_bar_size) {
            fpixel.y += scroll_y;
        } else {
            fpixel.y += uFontPx.y;
        }

        ivec2 pixel = ivec2(fpixel);
        ivec2 cell = pixel / uFontPx; 
        uvec4 char_attrib = texelFetch(uText, cell, 0);
        int thechar = int(char_attrib.r) - 32;
        vec4 fg = vec4(1.0);
        vec4 bg = vec4(1.0);
        // bbbfffcc -> bb bf ff cc ( A B G R )
        fg.x = float(char_attrib.b & 15u) * (1.f / 15.f);
        fg.y = float(char_attrib.g >> 4u) * (1.f / 15.f);
        fg.z = float(char_attrib.g & 15u) * (1.f / 15.f);
        bg.x = float(char_attrib.a >> 4) * (1.f / 15.f);
        bg.y = float(char_attrib.a & 15u) * (1.f / 15.f);
        bg.z = float(char_attrib.b >> 4) * (1.f / 15.f);
        if (bg.x == 0. && bg.y == 0. && bg.z == 0.) {
            bg.w = 0.0;
            if (grey > 0.5) {
                fg.xyz = vec3(1.) - fg.xyz;
            }    
        }
        
        vec2 fontpix = vec2(pixel - cell * uFontPx + 0.5f) / vec2(uFontPx);
        fontpix.x += (thechar & 15);
        fontpix.y += (thechar >> 4);
        fontpix *= vec2(1./16.,1./6.); // 16 x 6 atlas
        float aa = uFontPx.x / 2.f;
        float fontlvl = (thechar >= 0) ? sqrt(saturate((texture(uFont, fontpix).r-0.5) * aa + 0.5)) : 0.0;
        vec4 fontcol = mix(bg, fg, fontlvl);
        vec2 fcursor = vec2(cursor.x, cursor.y) - fpixel;
        vec2 fcursor_prev = vec2(cursor.z, cursor.w) - fpixel;
        vec2 cursor_delta = fcursor_prev - fcursor;
        float cursor_t = clamp(-fcursor.x / (cursor_delta.x + 1e-6 * sign(cursor_delta.x)), 0., 1.);
        fcursor += cursor_delta * cursor_t;
        if (fcursor.x >= -2.f && fcursor.x <= 2.f && fcursor.y >= -float(uFontPx.y) && fcursor.y <= 0.f) {
            float cursor_alpha = 1.f - cursor_t * cursor_t;
            fontcol = mix(fontcol, vec4(vec3(cursor_col), 1.), cursor_alpha);
        }
        o_color = vec4(rendercol.xyz * (1.-fontcol.w) + fontcol.xyz, 1.0);
    });
// clang-format on

EditableString edit_str = {};
STB_TexteditState state = {};
GLuint prog = 0, prog2 = 0;
GLuint vs = 0, fs = 0;
GLuint loc_uScreenPx2 = 0;
GLuint loc_iFrame2 = 0;
GLuint loc_iTime2 = 0;
GLuint loc_uFP2 = 0;
GLuint try_to_compile_shader(void) {
    char *fs2_str = (char *)calloc(1, strlen(kFS2_prefix) + stbds_arrlen(edit_str.str) + strlen(kFS2_suffix) + 64);
    sprintf(fs2_str, "%s\n#line 1\n%.*s\n%s", kFS2_prefix, (int)stbds_arrlen(edit_str.str), edit_str.str, kFS2_suffix);
    GLuint fs2 = compile_shader(GL_FRAGMENT_SHADER, fs2_str);
    GLuint new_prog2 = fs2 ? link_program(vs, fs2) : 0;
    if (new_prog2) {
        glDeleteProgram(prog2);
        prog2 = new_prog2;
        loc_uScreenPx2 = glGetUniformLocation(prog2, "uScreenPx");
        loc_iFrame2 = glGetUniformLocation(prog2, "iFrame");
        loc_iTime2 = glGetUniformLocation(prog2, "iTime");
        loc_uFP2 = glGetUniformLocation(prog2, "uFP");
    }
    if (!prog2)
        die("Failed to compile shader");
    glDeleteShader(fs2);
    free(fs2_str);
    GLenum e = glGetError(); // clear any errors
    return new_prog2;
}

int fbw, fbh; // current framebuffer size in pixels
int tmw, tmh; // current textmap size in cells
float retina = 1.0f;

static void adjust_font_size(int delta) {
    float yzoom = edit_str.cursor_y;
    edit_str.scroll_y_target = edit_str.scroll_y_target / edit_str.font_height - yzoom;
    edit_str.scroll_y = edit_str.scroll_y / edit_str.font_height - yzoom;
    edit_str.font_width = clampi(edit_str.font_width + delta, 8, 256);
    edit_str.font_height = edit_str.font_width * 2;
    edit_str.scroll_y_target = (edit_str.scroll_y_target + yzoom) * edit_str.font_height;
    edit_str.scroll_y = (edit_str.scroll_y + yzoom) * edit_str.font_height;
}

static int find_line_index(const char *str, int n, int pos) {
    int y = 0;
    for (int i = 0; i < n && i < pos; i++) {
        if (str[i] == '\n' || str[i] == 0) {
            ++y;
        }
    }
    return y;
}

static void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (key == GLFW_KEY_F4 && (mods == GLFW_MOD_CONTROL || mods == GLFW_MOD_SUPER || mods== GLFW_MOD_ALT)) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        if (key == GLFW_KEY_ESCAPE && mods == 0) {
            state.select_start = state.select_end = state.cursor;
        }

        if (key == GLFW_KEY_TAB)
            key = '\t';
        if (key == GLFW_KEY_ENTER)
            key = '\n';

        if (key < 32 || key > 126 || (mods & GLFW_MOD_SUPER)) {
            if (mods == GLFW_MOD_SUPER) {
                if (key == GLFW_KEY_C || key == GLFW_KEY_X) {
                    int se = mini(state.select_end, state.select_start);
                    int ee = maxi(state.select_end, state.select_start);
                    char *str = calloc(1, ee - se + 1);
                    memcpy(str, edit_str.str + se, ee - se);
                    glfwSetClipboardString(win, str);
                    free(str);
                    if (key == GLFW_KEY_X) {
                        stb_textedit_cut(&edit_str, &state);
                    }
                }
                if (key == GLFW_KEY_V) {
                    const char *str = glfwGetClipboardString(win);
                    if (str) {
                        stb_textedit_paste(&edit_str, &state, (STB_TEXTEDIT_CHARTYPE *)str, strlen(str));
                    }
                }
                if (key == GLFW_KEY_SLASH) {
                    int ss = mini(state.select_start, state.select_end);
                    int se = maxi(state.select_start, state.select_end);
                    if (ss==se) ss=se=state.cursor;
                    int first_line = find_line_index(edit_str.str, stbds_arrlen(edit_str.str), ss);
                    int last_line = find_line_index(edit_str.str, stbds_arrlen(edit_str.str), se);
                    int y = 0;
                    for (int i = 0; i < arrlen(edit_str.str); i++) {
                        if (y>=first_line && y<=last_line) {
                            while (i<arrlen(edit_str.str) && edit_str.str[i]!='\n' && edit_str.str[i]!=0 && is_space(edit_str.str[i])) i++;
                            // if it starts with //, remove it. otherwise add it.
                            if (i+1<arrlen(edit_str.str) && edit_str.str[i]=='/' && edit_str.str[i+1]=='/') {
                                arrdeln(edit_str.str, i, 2);
                                if (i<arrlen(edit_str.str) && edit_str.str[i]==' ')
                                    arrdel(edit_str.str, i);
                            } else {
                                arrinsn(edit_str.str, i, 3);
                                edit_str.str[i]='/';
                                edit_str.str[i+1]='/';
                                edit_str.str[i+2]=' ';
                            }
                        }
                        while (i<arrlen(edit_str.str) && edit_str.str[i]!='\n' && edit_str.str[i]!=0) i++;
                        y++;
                    }

                }
                if (key == GLFW_KEY_A) {
                    state.select_start = 0;
                    state.select_end = stbds_arrlen(edit_str.str);
                }
                if (key == GLFW_KEY_S) {
                    bool compiled = try_to_compile_shader() != 0;
                    if (compiled) {
                        FILE *f = fopen("f.tmp", "w");
                        if (f) {
                            fwrite(edit_str.str, 1, stbds_arrlen(edit_str.str), f);
                            fclose(f);
                            if (rename("f.tmp", "f.glsl")==0) {
                                set_status_bar(C_OK, "saved shader");
                            } else {
                                f=0;
                            }
                        }
                        if (!f) {
                            set_status_bar(C_ERR, "failed to save shader");
                        }                
                    }
                }
    
                if (key == GLFW_KEY_ENTER || key == '\n') {
                    try_to_compile_shader();
                }
                if (key == GLFW_KEY_MINUS) {
                    adjust_font_size(-1);
                }
                if (key == GLFW_KEY_EQUAL) {
                    adjust_font_size(1);
                }
            }
            // printf("key: %d, mods: %d\n", key, mods);
            stb_textedit_key(&edit_str, &state, key | (mods << 16));
            // printf("cursor %d , select_start %d, select_end %d\n", state.cursor, state.select_start, state.select_end);
            edit_str.need_scroll_update = true;
        }
    }
}

static void scroll_callback(GLFWwindow *win, double xoffset, double yoffset) {
    // printf("scroll: %f\n", yoffset);
    edit_str.scroll_y_target -= yoffset * edit_str.font_height;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    // printf("char: %d\n", codepoint);
    stb_textedit_key(&edit_str, &state, codepoint);
    edit_str.need_scroll_update = true;
}

static void mouse_button_callback(GLFWwindow *win, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
        stb_textedit_click(&edit_str, &state, mx - 64., my + edit_str.scroll_y);
        edit_str.need_scroll_update = true;
    }
    if (action == GLFW_RELEASE) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
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
    int ww = want_fullscreen ? vm->width : 1920 / 2;
    int wh = want_fullscreen ? vm->height : 1200 / 2;

    GLFWwindow *win = glfwCreateWindow(ww, wh, "ginkgo", mon, NULL);
    if (!win)
        die("glfwCreateWindow failed");
    glfwGetWindowContentScale(win, &retina, NULL);
    printf("retina: %f\n", retina);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    return win;
}


#define UC(lit, i, p) ((unsigned)p * (unsigned)(unsigned char)((lit)[i]))

#define HASH_(x)                                                                                                                   \
    UC(x, 0, 2) + UC(x, 1, 3) + UC(x, 2, 5) + UC(x, 3, 7) + UC(x, 4, 11) + UC(x, 5, 13) + UC(x, 6, 17) + UC(x, 7, 19) +            \
        UC(x, 8, 23) + UC(x, 9, 29) + UC(x, 10, 31) + UC(x, 11, 37) + UC(x, 12, 41) + UC(x, 13, 43) + UC(x, 14, 47) +              \
        UC(x, 15, 53)

#define HASH(lit) HASH_(lit "                ") /* 16 spaces */

static inline unsigned hash_span(const char *s, const char *e) {
    static const unsigned primes[16] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53};
    unsigned h = 0;
    for (int i = 0; i < 16; ++i, ++s) {
        unsigned c = (s < e) ? *s : ' ';
        h += c * primes[i];
    }
    return h;
}

// detect if '+'/'-' is unary (belongs to number) based on previous non-space char
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

// --- simple LIFO for brackets ---

#define MAX_BRACK 256
typedef struct tokenizer_t {
    int n;           // num chars
    const char *str; // source
    char stk[MAX_BRACK];
    int sp, paren, brack, brace;
    int x, y;           // cursor position
    uint32_t *ptr;      // destination
    char prev_nonspace; // previous non-space char
} tokenizer_t;

char tok_get(tokenizer_t *t, int i) { return (i < t->n && i >= 0) ? t->str[i] : 0; }

#define PUSH_BRACKET(opc, count)                                                                                                   \
    if (t.sp < MAX_BRACK)                                                                                                          \
        t.stk[t.sp++] = opc;                                                                                                       \
    count++;                                                                                                                       \
    col = C_PUN;

#define CLOSE_BRACKET(opc, count)                                                                                                  \
    col = (t.sp > 0 && t.stk[t.sp - 1] == opc) ? C_PUN : C_ERR;                                                                    \
    if (t.sp > 0)                                                                                                                  \
        t.sp--;                                                                                                                    \
    if (count > 0)                                                                                                                 \
        count--;

int code_color(uint32_t *ptr) {
    int left = 64 / edit_str.font_width;
    tokenizer_t t = {.ptr = ptr,
                     .str = edit_str.str,
                     .n = stbds_arrlen(edit_str.str),
                     .x = left,
                     .y = -edit_str.intscroll,
                     .prev_nonspace = ';'};
    bool wasinclude = false;
    int se = mini(state.select_start, state.select_end);
    int ee = maxi(state.select_start, state.select_end);
    for (int i = 0; i <= t.n + 10;) {
        unsigned h = 0;
        char c = tok_get(&t, i);
        uint32_t col = C_DEF;
        int j = i + 1;
        switch (c) {
        case '(':
            PUSH_BRACKET('(', t.paren);
            break;
        case '[':
            PUSH_BRACKET('[', t.brack);
            break;
        case '{':
            PUSH_BRACKET('{', t.brace);
            break;
        case ')':
            CLOSE_BRACKET('(', t.paren);
            break;
        case ']':
            CLOSE_BRACKET('[', t.brack);
            break;
        case '}':
            CLOSE_BRACKET('{', t.brace);
            break;
        case '/': {
            char c2 = tok_get(&t, i + 1);
            if (c2 == '/') {
                col = C_COM;
                j = scan_line_comment(t.str, i, t.n);
            } else if (c2 == '*') {
                col = C_COM;
                j = scan_block_comment(t.str, i, t.n);
            }
        } break;
        case '"': {
            col = C_STR;
            j = scan_string(t.str, i, t.n, '"');
        } break;
        case '<':

            if (wasinclude) {
                col = C_STR;
                j = scan_string(t.str, i, t.n, '>');
            } else
                col = C_PUN;
            break;
        case '\'': {
            col = C_STR;
            j = scan_string(t.str, i, t.n, '\'');
        } break;
        default: {
            if (is_ident_start((unsigned)c)) {
                col = C_DEF;
                j = scan_ident(t.str, i, t.n);
                h = hash_span(t.str + i, t.str + j);
                switch (h) {
                case HASH("void"):
                case HASH("float"):
                case HASH("double"):
                case HASH("int"):
                case HASH("uint"):
                case HASH("char"):
                case HASH("short"):
                case HASH("long"):
                case HASH("signed"):
                case HASH("unsigned"):
                case HASH("bool"):
                case HASH("size_t"):
                case HASH("ptrdiff_t"):
                case HASH("ssize_t"):
                case HASH("off_t"):
                case HASH("time_t"):
                case HASH("int8_t"):
                case HASH("uint8_t"):
                case HASH("int16_t"):
                case HASH("uint16_t"):
                case HASH("uint32_t"):
                case HASH("uint64_t"):
                case HASH("int32_t"):
                case HASH("int64_t"):
                case HASH("float32_t"):
                case HASH("float64_t"):
                case HASH("vec2"):
                case HASH("vec3"):
                case HASH("vec4"):
                case HASH("mat2"):
                case HASH("mat3"):
                case HASH("mat4"):
                case HASH("sampler2D"):
                case HASH("sampler3D"):
                    col = C_TYPE;
                    break;
                case HASH("if"):
                case HASH("else"):
                case HASH("for"):
                case HASH("while"):
                case HASH("do"):
                case HASH("switch"):
                case HASH("case"):
                case HASH("break"):
                case HASH("continue"):
                case HASH("return"):
                case HASH("uniform"):
                case HASH("in"):
                case HASH("out"):
                case HASH("layout"):
                case HASH("struct"):
                case HASH("class"):
                case HASH("enum"):
                case HASH("union"):
                case HASH("typedef"):
                case HASH("static"):
                case HASH("extern"):
                case HASH("inline"):
                case HASH("volatile"):
                case HASH("const"):
                case HASH("register"):
                case HASH("restrict"):
                    col = C_KW;
                    break;
                case HASH("define"):
                case HASH("pragma"):
                    col = C_PREPROC;
                    break;
                case HASH("include"):
                    wasinclude = true;
                    col = C_PREPROC;
                    break;
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
        for (; i < j; ++i) {
            char ch = tok_get(&t, i);
            uint32_t ccol = col << 8;
            if (i >= se && i < ee)
                ccol = C_SELECTION << 8;

            if (i == state.cursor) {
                edit_str.cursor_x = t.x;
                edit_str.cursor_y = t.y + edit_str.intscroll;
            }
            if (t.x < TMW && t.y >= 0 && t.y < TMH)
                t.ptr[t.y * TMW + t.x] = (ccol) | (unsigned char)(ch);
            if (ch == '\t')
                t.x += 2;
            else if (ch == '\n' || ch == 0) {
                // look for an error message
                const char *errline = hmget(error_msgs, t.y);
                if (errline) {
                    for (; *errline && *errline != '\n'; errline++) {
                        if (t.x < TMW && t.y >= 0 && t.y < TMH)
                            t.ptr[t.y * TMW + t.x] = (C_ERR << 8) | (unsigned char)(*errline);
                        t.x++;
                    }
                }
                t.x = left;
                ++t.y;
            } else
                ++t.x;
            if (!is_space((unsigned)ch))
                t.prev_nonspace = ch;
        }
    }
    edit_str.num_lines = t.y + 1 + edit_str.intscroll;
    return edit_str.num_lines;
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
    cfg.sampleRate = SAMPLE_RATE_OUTPUT;
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


    vs = compile_shader(GL_VERTEX_SHADER, kVS);
    fs = compile_shader(GL_FRAGMENT_SHADER, kFS);
    prog = link_program(vs, fs);
    glDeleteShader(fs);

    FILE *f = fopen("f.glsl", "r");
    assert(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    stbds_arrsetlen(edit_str.str, len);
    fseek(f, 0, SEEK_SET);
    fread(edit_str.str, 1, len, f);
    fclose(f);
    try_to_compile_shader();

    GLint loc_uText = glGetUniformLocation(prog, "uText");
    GLint loc_uFont = glGetUniformLocation(prog, "uFont");
    GLint loc_uFP = glGetUniformLocation(prog, "uFP");
    GLint loc_iTime = glGetUniformLocation(prog, "iTime");
    GLint loc_scroll_y = glGetUniformLocation(prog, "scroll_y");
    GLint loc_cursor = glGetUniformLocation(prog, "cursor");
    GLint loc_uTextSize = glGetUniformLocation(prog, "uTextSize");
    GLint loc_uCellPx = glGetUniformLocation(prog, "uCellPx");
    GLint loc_uScreenPx = glGetUniformLocation(prog, "uScreenPx");
    GLint loc_uFontPx = glGetUniformLocation(prog, "uFontPx");
    GLint loc_status_bar_size = glGetUniformLocation(prog, "status_bar_size");
    int fw = 0, fh = 0, fc = 0;
    stbi_uc *fontPixels = stbi_load("assets/font_sdf.png", &fw, &fh, &fc, 4);
    if (!fontPixels)
        die("Failed to load font");
    assert(fw == 32 * 16 && fh == 64 * 6);
    GLuint texFont = gl_create_texture(GL_LINEAR, GL_CLAMP_TO_EDGE);
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
        GLenum bufs[1] = {GL_COLOR_ATTACHMENT0};
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

    edit_str.font_width = 12;
    edit_str.font_height = 24;
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
        mx *= retina;
        my *= retina;
        int m0 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        int m1 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (m0) {
            stb_textedit_drag(&edit_str, &state, mx - 64., my + edit_str.scroll_y);
            edit_str.need_scroll_update = true;
        }

        glfwGetFramebufferSize(win, &fbw, &fbh);
        // printf("fbw: %d, fbh: %d\n", fbw, fbh);
        tmw = fbw / edit_str.font_width;
        tmh = fbh / edit_str.font_height;
        if (tmw > 512)
            tmw = 512;
        if (tmh > 256 - 8)
            tmh = 256 - 8;

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
            edit_str.intscroll = (int)(edit_str.scroll_y / edit_str.font_height);
            code_color(ptr);
            if (status_bar_time > glfwGetTime() - 3.0 && status_bar_color) {
                int x = 0;
                for (const char *c = status_bar; *c && *c != '\n' && x < TMW; c++, x++) {
                    ptr[tmh * TMW + x] = (status_bar_color << 8) | (unsigned char)(*c);
                }
            } else {
               status_bar_color = 0;
            }
            if (edit_str.need_scroll_update) {
                edit_str.need_scroll_update = false;
                if (edit_str.cursor_y >= edit_str.intscroll + tmh - 4) {
                    edit_str.scroll_y_target = (edit_str.cursor_y - tmh + 4) * edit_str.font_height;
                } else if (edit_str.cursor_y < edit_str.intscroll + 4) {
                    edit_str.scroll_y_target = (edit_str.cursor_y - 4) * edit_str.font_height;
                }
            }
            edit_str.scroll_y_target = clampf(edit_str.scroll_y_target, 0, (edit_str.num_lines - tmh + 4) * edit_str.font_height);
            // now find a zero crossing in the scope, and copy the relevant section
            // scope cant be more than 2048 as that's how many slots we have in the texture.
            uint32_t scope_start = scope_pos - 1024;
            uint32_t scan_max = 1024;
            int bestscani = 0;
            float bestscan = 0.f;
            for (int i = 1; i < scan_max; ++i) {
                float mono = mid(scope[(scope_start - i) & SCOPE_MASK]);
                float mono_next = mid(scope[(scope_start - i + 1) & SCOPE_MASK]);
                float delta = mono_next - mono;
                if (mono < 0.f && mono_next > 0.f && delta > bestscan) {
                    bestscan = delta;
                    bestscani = i;
                    break;
                }
            }
            scope_start -= 1024 + bestscani;
            uint32_t *scope_dst = ptr + (TMH - 4) * TMW;
            for (int i = 0; i < 2048; ++i) {
                stereo sc = scope[(scope_start + i) & SCOPE_MASK];
                uint8_t l8 = (uint8_t)(clampf(sc.l, -1.f, 1.f) * 127.f + 128.f);
                uint8_t r8 = (uint8_t)(clampf(sc.r, -1.f, 1.f) * 127.f + 128.f);
                scope_dst[i] = (l8 << 0) | (r8 << 8);
            }

            uint32_t fft_start = scope_pos - FFT_SIZE;
            float fft_buf[2][FFT_SIZE];
            for (int i = 0; i < FFT_SIZE; ++i) {
                stereo sc = scope[(fft_start + i) & SCOPE_MASK];
                fft_buf[0][i] = sc.l * fft_window[i];
                fft_buf[1][i] = sc.r * fft_window[i];
            }
            pffft_transform_ordered(fft_setup, fft_buf[0], fft_buf[0], fft_work, PFFFT_FORWARD);
            pffft_transform_ordered(fft_setup, fft_buf[1], fft_buf[1], fft_work, PFFFT_FORWARD);
            scope_dst = ptr + (TMH - 12) * TMW;
            float peak_mag = squared2db(squaref(0.25f * FFT_SIZE)); // assuming hann, coherent gain is 0.5
            // int mini=0, maxi=0;
            // float minv=1000.f, maxv=-1000.f;
            for (int i = 0; i < 4096; ++i) {
                float magl_db =
                    squared2db(fft_buf[0][i * 2] * fft_buf[0][i * 2] + fft_buf[0][i * 2 + 1] * fft_buf[0][i * 2 + 1]) - peak_mag;
                float magr_db =
                    squared2db(fft_buf[1][i * 2] * fft_buf[1][i * 2] + fft_buf[1][i * 2 + 1] * fft_buf[1][i * 2 + 1]) - peak_mag;
                // if (magl_db < minv) {minv = magl_db;mini = i;}
                // if (magl_db > maxv) {maxv = magl_db;maxi = i;}
                uint8_t l8 = (uint8_t)(clampf(255.f + magl_db * 4.f, 0.f, 255.f));
                uint8_t r8 = (uint8_t)(clampf(255.f + magr_db * 4.f, 0.f, 255.f));
                scope_dst[i] = (l8 << 0) | (r8 << 8);
            }
            // printf("fft min: %f in bin %d, max: %f in bin %d\n", minv, mini, maxv, maxi);

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
        // glClearColor((iFrame&1), 0.f, (iFrame&1)^1, 1.f);
        // glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog2); // a fragment shader that writes RGBA16F
        check_gl("use prog2");
        glUniform2i(loc_uScreenPx2, RESW, RESH);
        check_gl("uniform uScreenPx2");
        glUniform1ui(loc_iFrame2, iFrame);
        glUniform1f(loc_iTime2, t);
        check_gl("uniform iFrame");

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texFPRT[(iFrame + 1) % 2]);
        glUniform1i(loc_uFP2, 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        check_gl("draw fpRT");

        // glClearColor(0.f, 0.f, 0.f, 1.f);
        // glClear(GL_COLOR_BUFFER_BIT);

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
        glUniform2i(loc_uFontPx, edit_str.font_width, edit_str.font_height);
        glUniform1i(loc_status_bar_size, status_bar_color ? 1 : 0);
        glUniform1f(loc_iTime, t);
        glUniform1f(loc_scroll_y, edit_str.scroll_y - edit_str.intscroll * edit_str.font_height);
        float f_cursor_x = edit_str.cursor_x * edit_str.font_width;
        float f_cursor_y = (edit_str.cursor_y - edit_str.intscroll) * edit_str.font_height;
        glUniform4f(loc_cursor, f_cursor_x, f_cursor_y, edit_str.prev_cursor_x, edit_str.prev_cursor_y);
        edit_str.prev_cursor_x += (f_cursor_x - edit_str.prev_cursor_x) * 0.2f;
        edit_str.prev_cursor_y += (f_cursor_y - edit_str.prev_cursor_y) * 0.2f;
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
