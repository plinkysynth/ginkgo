
// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#define MINIAUDIO_IMPLEMENTATION
#define PFFFT_IMPLEMENTATION
#include <stdio.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <curl/curl.h>
#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "3rdparty/pffft.h"
#include "utils.h"
#include "ginkgo.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers
#include "text_editor.h"
#include "audio_host.h"
#include "midi_mac.h"
#include "hash_literal.h"
#include "miniparse.h"
#include "sampler.h"

char status_bar[512];
double status_bar_time = 0;
uint32_t status_bar_color = 0;

// size of virtual textmode screen:
#define TMW 512
#define TMH 256

// --- colors 3 digits bg, 3 digits fg, 2 digits character.
#define C_SELECTION 0xc48fff00u
#define C_SELECTION_FIND_MODE 0x4c400000u
#define C_NUM 0x000cc700u
#define C_DEF 0x000fff00u
#define C_KW 0x0009ac00u
#define C_TYPE 0x000a9c00u
#define C_PREPROC 0x0009a900u
#define C_STR 0x0008df00u
#define C_COM 0x000fd800u
#define C_PUN 0x000aaa00u
#define C_ERR 0xf00fff00u
#define C_OK 0x080fff00u
#define C_WARNING 0xfa400000u
#define C_CHART 0x1312a400u // mini notation chart
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

void set_status_bar(uint32_t color, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vsnprintf(status_bar, sizeof(status_bar) - 1, msg, args);
    va_end(args);
    fprintf(stderr, "%s\n", status_bar);
    status_bar_color = color;
    status_bar_time = glfwGetTime();
}

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
        rendercol = max(vec3(0.), rendercol);
        float grey = dot(rendercol, vec3(0.2126 * 1.5, 0.7152 * 1.5, 0.0722 * 1.5));

        vec2 pix = v_uv * vec2(uScreenPx.x, 2048.f);
        float fftx = uScreenPx.x - pix.x;
        if (pix.x < 64.f || fftx<128.f) {
            bool is_fft = false;
            float ampscale = 0.25f;
            int base_y = 256 - 4;
            float nextx = pix.y + 1.;
            if (fftx < 128.f) {
                ampscale = 0.5f;
                is_fft = true;
                float dither = fract(pix.x*23.5325f) / 2048.f;
                pix.y = (48.f / 48000.f * 4096.f) * pow(1000.f, v_uv.y + dither); // 24hz to nyquist
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
                beam *= 0.25;
            } else {
                beam *= smoothstep(miny - 2.f, maxy - 0.5f, pix.xx);
                beam *= 1.5f / (maxy - miny + 1.f);
            }
            vec3 beamcol = vec3(0.2, 0.4, 0.8) * beam.x + vec3(0.8, 0.4, 0.2) * beam.y;
            if (gl_FragCoord.x<64. && gl_FragCoord.y<64.) {
                int idx = int(gl_FragCoord.x) + int(gl_FragCoord.y) * 64;
                uvec4 xyscope = texelFetch(uText, ivec2(idx & 511, (256-20) + (idx >> 9)), 0);
                beamcol = vec3(float(xyscope.x + xyscope.y * 256u) * vec3(0.00004,0.00008,0.00006));
            }
            if (grey > 0.5) {
                beamcol = -beamcol * 2.;
            }
            rendercol += beamcol;
        }
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
        int ascii = int(char_attrib.r);
        vec4 fg = vec4(1.0), bg = vec4(1.0);
        fg.x = float(char_attrib.b & 15u) * (1.f / 15.f);
        fg.y = float(char_attrib.g >> 4u) * (1.f / 15.f);
        fg.z = float(char_attrib.g & 15u) * (1.f / 15.f);
        bg.x = float(char_attrib.a >> 4) * (1.f / 15.f);
        bg.y = float(char_attrib.a & 15u) * (1.f / 15.f);
        bg.z = float(char_attrib.b >> 4) * (1.f / 15.f);
        if (bg.x <= 1.f/15.f && bg.y <= 1.f/15.f && bg.z <= 1.f/15.f) {
            bg.w = bg.x * 4.f;
            bg.xyz = vec3(0.f);
            if (grey > 0.5) {
                fg.xyz = vec3(1.) - fg.xyz;
            }    
        }
        
        vec2 cellpix = vec2(pixel - cell * uFontPx + 0.5f) / vec2(uFontPx);
        vec2 fontpix=vec2(cellpix.x + (ascii & 15), cellpix.y + (ascii >> 4)-2) * vec2(1./16.,1./6.);
        float sdf_level = 0.f;
        float aa = uFontPx.x / 2.f;
        if (ascii<128) {
            sdf_level = texture(uFont, fontpix).r - 0.5f;
        } else if (ascii<128+16) {
            // vertical bar:
            sdf_level = 4.f * (cellpix.y -1.f + float(ascii-128+1)*(1.f/16.f)); // bar graph
        } else {
            // horizontal slider tick:
            sdf_level = 0.2f - 4.f * abs(cellpix.y-0.5f);
            sdf_level=max(sdf_level, 0.5f - 2.f * abs(cellpix.x+0.25f-float(ascii-(128+16))*1.f/8.f));
        }
        float fontlvl = (ascii >= 32) ? sqrt(saturate(sdf_level * aa + 0.5)) : 0.0;
        vec4 fontcol = mix(bg, fg, fontlvl);
        vec2 fcursor = vec2(cursor.x, cursor.y) - fpixel;
        vec2 fcursor_prev = vec2(cursor.z, cursor.w) - fpixel;
        vec2 cursor_delta = fcursor_prev - fcursor;
        float cursor_t = clamp(-fcursor.x / (cursor_delta.x + 1e-6 * sign(cursor_delta.x)), 0., 1.);
        fcursor += cursor_delta * cursor_t;
        if (fcursor.x >= -2.f && fcursor.x <= 2.f && fcursor.y >= -float(uFontPx.y) && fcursor.y <= 0.f) {
            float cursor_alpha = 1.f - cursor_t * cursor_t;
            float cursor_col = (grey > 0.5) ? 0. : 1.;    
            fontcol = mix(fontcol, vec4(vec3(cursor_col), 1.), cursor_alpha);
        }
        o_color = vec4(rendercol.xyz * (1.-fontcol.w) + fontcol.xyz, 1.0);
    });
// clang-format on

EditorState audio_tab = {.fname = "livesrc/audio.c", .is_shader = false};
EditorState shader_tab = {.fname = "livesrc/video.glsl", .is_shader = true};
EditorState *curE = &audio_tab;
GLuint prog = 0, prog2 = 0;
GLuint vs = 0, fs = 0;
GLuint loc_uScreenPx2 = 0;
GLuint loc_iFrame2 = 0;
GLuint loc_iTime2 = 0;
GLuint loc_uFP2 = 0;
size_t textBytes = (size_t)(TMW * TMH * 4);
GLuint pbos[3];
int pbo_index = 0;
GLuint texFont = 0, texText = 0;

float *closest_slider[16] = {}; // 16 closest sliders to the cursor, for midi cc

static void parse_error_log(EditorState *E) {
    hmfree(E->error_msgs);
    E->error_msgs = NULL;
    int fnamelen = strlen(E->fname);
    for (const char *c = E->last_compile_log; *c; c++) {
        int col = 0, line = 0;
        if (sscanf(c, "ERROR: %d:%d:", &col, &line) == 2) {
            hmput(E->error_msgs, line - 1, c);
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
                hmput(E->error_msgs, line - 1, colend);
            }
        }
        while (*c && *c != '\n')
            c++;
    }
}

static GLuint compile_shader(EditorState *E, GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (E) {
        stbds_arrfree(E->last_compile_log);
        E->last_compile_log = NULL;
        stbds_hmfree(E->error_msgs);
        E->error_msgs = NULL;
    }
    if (!ok && E) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        stbds_arrsetlen(E->last_compile_log, len > 1 ? len : 1);
        char *last_compile_log = E->last_compile_log;
        glGetShaderInfoLog(s, len, NULL, last_compile_log);
        fprintf(stderr, "Shader compile failed:\n[%s]\n", last_compile_log);
        parse_error_log(E);
        set_status_bar(C_ERR, "Shader compile failed - %d errors", (int)hmlen(E->error_msgs));
    }
    if (!ok)
        return 0;
    return s;
}

GLuint try_to_compile_shader(EditorState *E) {
    if (!E->is_shader) {
        return 0;
    }
    char *fs2_str = (char *)calloc(1, strlen(kFS2_prefix) + stbds_arrlen(E->str) + strlen(kFS2_suffix) + 64);
    sprintf(fs2_str, "%s\n#line 1\n%.*s\n%s", kFS2_prefix, (int)stbds_arrlen(E->str), E->str, kFS2_suffix);
    GLuint fs2 = compile_shader(E, GL_FRAGMENT_SHADER, fs2_str);
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

float retina = 1.0f;

static void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods) {
    EditorState *E = curE;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    if (mods == 0) {

        if (key == GLFW_KEY_F1) {
            E = curE = &shader_tab;
        }
        if (key == GLFW_KEY_F2) {
            E = curE = &audio_tab;
        }
    }
    if (key == GLFW_KEY_F4 && (mods == GLFW_MOD_CONTROL || mods == GLFW_MOD_SUPER || mods == GLFW_MOD_ALT)) {
        glfwSetWindowShouldClose(win, GLFW_TRUE);
    }

    if (key == GLFW_KEY_TAB)
        key = '\t';
    if (key == GLFW_KEY_ENTER)
        key = '\n';
    if (mods == GLFW_MOD_SUPER) {
        if (key == GLFW_KEY_S) {
            bool compiled = true;
            if (!E->is_shader || try_to_compile_shader(E) != 0) {
                FILE *f = fopen("editor.tmp", "w");
                if (f) {
                    fwrite(E->str, 1, stbds_arrlen(E->str), f);
                    fclose(f);
                    if (rename("editor.tmp", E->fname) == 0) {
                        set_status_bar(C_OK, "saved shader");
                    } else {
                        f = 0;
                    }
                }
                if (!f) {
                    set_status_bar(C_ERR, "failed to save shader");
                }
            }
        }
        if (key == GLFW_KEY_ENTER || key == '\n') {
            if (E->is_shader)
                try_to_compile_shader(E);
        }
    }

    // printf("key: %d, mods: %d\n", key, mods);
    if ((mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) || key > 127 || key == '\b' || key == '\n' || key == '\t')
        editor_key(win, E, key | (mods << 16));
    // printf("cursor %d , select_start %d, select_end %d\n", state.cursor, state.select_start, state.select_end);
    E->need_scroll_update = true;
}

static void scroll_callback(GLFWwindow *win, double xoffset, double yoffset) {
    // printf("scroll: %f\n", yoffset);
    curE->scroll_y_target -= yoffset * curE->font_height;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    // printf("char: %d\n", codepoint);
    editor_key(win, curE, codepoint);
    curE->need_scroll_update = true;
}

static double click_mx, click_my, click_time;
static int click_count;

static void mouse_button_callback(GLFWwindow *win, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
        click_mx = mx;
        click_my = my;
        double t = glfwGetTime();
        if (t - click_time > 0.25) {
            click_count = 0;
        }
        click_time = t;
        editor_click(curE, G, mx, my, 0, click_count);
        curE->need_scroll_update = true;
    }
    if (action == GLFW_RELEASE) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
        double release_time = glfwGetTime();
        if (release_time - click_time < 0.4 && fabs(mx - click_mx) < 3.f && fabs(my - click_my) < 3.f) {
            click_count++;
        } else {
            click_count = 0;
        }
        editor_click(curE, G, mx, my, -1, click_count);
        curE->need_scroll_update = true;
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

void print_to_screen(uint32_t *ptr, int x, int y, uint32_t color, bool multi_line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(NULL, 0, fmt, args);
    if (length > TMW * 16)
        length = TMW * 16;
    va_end(args);
    va_start(args, fmt);
    char buf[length + 1];
    vsnprintf(buf, length + 1, fmt, args);
    buf[length] = 0;
    va_end(args);
    int leftx = x;
    if (y >= TMH)
        return;
    for (const char *c = buf; *c; c++) {
        if (*c == '\n') {
            if (!multi_line)
                break;
            x = leftx;
            y++;
            if (y >= TMH)
                return;
            continue;
        }
        if (y >= 0 && x < TMW && x >= 0)
            ptr[y * TMW + x] = (color) | (unsigned char)(*c);
        x++;
    }
}

int vertical_bar(int y) { // y=0 (empty) to y=16 (full)
    if (y <= 0)
        return ' ';
    return 128 + clampi(y - 1, 0, 15);
}

int horizontal_tick(int x) { // x=0 (4 pixel tick starting from left edge) to x=8 (off the right edge)
    if (x >= 8 || x < -4)
        return 128 + 16;
    return 128 + 16 + clampi(x + 4, 0, 15);
}

static inline int ispathchar(char c) { return isalnum(c) || c=='_' || c=='-' || c=='.'; }

const char *skip_path(const char *s, const char *e) {
    while (s < e && isspace(*s)) ++s;
    while (s < e) {
        if (*s != '/') return s;
        ++s; // skip /
        while (s < e && (ispathchar(*s))) ++s;
    }
    return s;
}


int code_color(EditorState *E, uint32_t *ptr) {
    int left = 64 / E->font_width;
    tokenizer_t t = {.ptr = ptr,
                     .str = E->str,
                     .n = stbds_arrlen(E->str),
                     .x = left,
                     .y = -E->intscroll,
                     .cursor_x = E->cursor_x,
                     .cursor_y = E->cursor_y - E->intscroll,
                     .prev_nonspace = ';'};
    bool wasinclude = false;
    int se = get_select_start(E);
    int ee = get_select_end(E);
    float *sliders[TMH] = {};
    int sliderindices[TMH];
    float *new_closest_slider[16] = {};
    if (G) {
        for (int slideridx = 0; slideridx < 16; ++slideridx) {
            for (int i = 0; i < G->sliders_hwm[slideridx]; i += 2) {
                float *value_line = G->sliders[slideridx].data + i;
                float value = value_line[0];
                int line = (int)value_line[1];
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
        }
        memcpy(closest_slider, new_closest_slider, sizeof(closest_slider));
    } else {
        memset(closest_slider, 0, sizeof(closest_slider));
    }
    int tmw = (fbw - 64.f) / E->font_width;
    E->cursor_in_pattern_area = false;
    int cursor_in_curve_start_idx = 0;
    int cursor_in_curve_end_idx = 0;
    int pattern_entry_idx = -1;
    int pattern_mode = 0; // if >0, we are parsing pattern not C; we code colour differently, and only exit when we leave.
    slider_spec_t slider_spec;
    for (int i = 0; i <= t.n + 10;) {
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
                    if (!(E->cursor_idx >= i && E->cursor_idx < j) || (G && G->mb!=0))
                        token_is_slider = true;
                    col = C_SLIDER;
                }
            } else if (pattern_mode) {
                col = C_STR;
                const char *e = skip_path(t.str + i, t.str + t.n);
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
        } break;
        case '\'': {
            col = C_STR;
            j = scan_string(t.str, i, t.n, '\'');
            if (pattern_mode && E->cursor_idx >= i && E->cursor_idx < j) {
                cursor_in_curve_start_idx = i;
                cursor_in_curve_end_idx = j;
            } else {
                token_is_curve = pattern_mode;
            }
        } break;
        case '#':
            col = C_PREPROC;
            if (!pattern_mode && i + 15 <= t.n && strncmp(t.str + i, "#ifdef PATTERNS", 15) == 0) {
                pattern_mode = 1;
                pattern_entry_idx = i;
                j = i + 15;
            } else if (pattern_mode && i + 6 <= t.n && strncmp(t.str + i, "#endif", 6) == 0) {
                if (E->cursor_idx >= pattern_entry_idx && E->cursor_idx < i) {
                    E->cursor_in_pattern_area = true;
                }
                pattern_mode = 0;
                j = i + 6;
            }
            break;

        default: {
            if (is_ident_start((unsigned)c)) {
                col = C_DEF;
                j = scan_ident(t.str, i, t.n);
                h = literal_hash_span(t.str + i, t.str + j);
                int midinote = parse_midinote(t.str + i, t.str + j, 1);
                if (midinote >= 0) {
                    col = C_NOTE;
                } else
                    switch (h) {
                        CASE8("void", "float", "double", "int", "uint", "char", "short", "long")
                            : CASE8("signed", "unsigned", "bool", "size_t", "ptrdiff_t", "ssize_t", "off_t", "time_t")
                            : CASE8("int8_t", "uint8_t", "int16_t", "uint16_t", "uint32_t", "uint64_t", "float32_t", "float64_t")
                            : CASE8("vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "sampler2D", "sampler3D") : col = C_TYPE;
                        break;
                        CASE8("if", "else", "for", "while", "do", "switch", "case", "break")
                            : CASE8("continue", "return", "uniform", "in", "out", "layout", "struct", "class")
                            : CASE8("enum", "union", "typedef", "static", "extern", "inline", "volatile", "const")
                            : CASE2("register", "restrict") : col = C_KW;
                        break;
                        CASE2("define", "pragma") : col = C_PREPROC;
                        break;
                        CASE8("S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7") : CASE2("S8", "S9") : {
                            int slideridx = (t.str[i + 1] - '0');
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll)
                                slideridx = 16;
                            col = slidercols[slideridx];
                            break;
                        }
                        CASE6("S10", "S11", "S12", "S13", "S14", "S15") : {
                            int slideridx = ((t.str[i + 2] - '0') + 10);
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll)
                                slideridx = 16;
                            col = slidercols[slideridx];
                            break;
                        }
                        CASE6("SA", "SB", "SC", "SD", "SE", "SF") : {
                            int slideridx = (t.str[i + 1] - 'A' + 10);
                            if (closest_slider[slideridx] == NULL || closest_slider[slideridx][1] != t.y + E->intscroll)
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
        if (token_is_curve) {
            int numdata = j - i - 2;
            curve_data = alloca(4 * numdata);
            fill_curve_data_from_string(curve_data, t.str + i + 1, numdata);
        } 
        int starti = i;
        int slider_val = 0;
        if (token_is_slider && slider_spec.maxval != slider_spec.minval && j>i+2) {
            float v = (slider_spec.curval - slider_spec.minval) / (slider_spec.maxval - slider_spec.minval);
            slider_val = (int)(v * ((j-i-2) * 8.f - 4.f));
        }
        for (; i < j; ++i) {
            char ch = tok_get(&t, i);
            uint32_t ccol = col;
            if (curve_data) {
                ccol = C_CHART;
                if (i > starti && i < j - 1) {
                    ch = vertical_bar((int)(curve_data[i - starti - 1] * 16.f + 0.5f));
                }
            } else if (token_is_slider && i>starti && i<j-1) {
                ch = horizontal_tick(slider_val - (i - starti - 1) * 8);
            }
            if (i >= se && i < ee)
                ccol = E->find_mode ? C_SELECTION_FIND_MODE : C_SELECTION;

            if (i == E->cursor_idx) {
                E->cursor_x = t.x;
                E->cursor_y = t.y + E->intscroll;
            }
            uint32_t bgcol = ccol & 0xfff00000u;
            if (pattern_mode && bgcol == 0) {
                // pattern area gets a special bg color
                ccol |= 0x11100000u;
            }
            if (t.x < TMW && t.y >= 0 && t.y < TMH)
                t.ptr[t.y * TMW + t.x] = (ccol) | (unsigned char)(ch);
            if (ch == '\t')
                t.x = next_tab(t.x - left) + left;
            else if (ch == '\n' || ch == 0) {
                // look for an error message
                const char *errline = hmget(E->error_msgs, t.y);
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
                // fill to the end of the line
                if (pattern_mode) {
                    for (; t.x < tmw; t.x++) {
                        t.ptr[t.y * TMW + t.x] = ccol | (unsigned char)(' ');
                    }
                }
                if (t.y >= 0 && t.y < TMH && sliders[t.y]) {
                    int slider_idx = sliderindices[t.y];
                    float value = *sliders[t.y];
                    int x1 = maxi(0, tmw - 16);
                    int x2 = tmw;
                    uint32_t col = slidercols[slider_idx];
                    if (closest_slider[slider_idx] == NULL || closest_slider[slider_idx][1] != t.y + E->intscroll)
                        col = C_SLIDER;
                    for (int x = x1; x < x2; x++) {
                        int ch = horizontal_tick((int)(value * 127.f) - (x - x1) * 8 - 2);
                        t.ptr[t.y * TMW + x] = col | ch;
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
    E->num_lines = t.y + 1 + E->intscroll;
    E->mouse_hovering_chart = false;

    if (E->cursor_in_pattern_area) {
        // draw a popup below the cursor
        int basex = left;
        int chartx = basex + 13;    
        int code_start_idx = xy_to_idx(E, 0, E->cursor_y);
        const char *s = skip_path(t.str + code_start_idx, t.str + t.n);
        code_start_idx = s - t.str;
        int code_end_idx = xy_to_idx(E, 0x7fffffff , E->cursor_y);
        static uint32_t cached_compiled_string_hash = 0;
        static Pattern cached_parser;
        static Hap *cached_haps;
        const char *codes = t.str + code_start_idx;
        const char *codee = t.str + code_end_idx;
        uint32_t hash = fnv1_hash(codes, codee);
        if (hash!=cached_compiled_string_hash) {
            cached_compiled_string_hash = hash;
            cached_parser.s = codes;
            cached_parser.n = codee - codes;
            parse_pattern(&cached_parser);
            if (cached_parser.err<=0) {
                stbds_arrsetlen(cached_haps, 0);
                make_haps(&cached_parser, cached_parser.root, 0.f, 4.f, 1.f, 0.f, &cached_haps, FLAG_NONE, 0, 1.f);
            }
        }
        int nhaps = cached_haps ? stbds_arrlen(cached_haps) : 0;
        if (nhaps) {
            struct { Sound *key; int value; } *rows = NULL;
            int numrows = 0;
            const Node *nodes = cached_parser.nodes;
            int ss = get_select_start(E);
            int se = get_select_end(E);
            for (int i=0; i<nhaps; i++) {
                const Hap *h = &cached_haps[i];
                Sound *sound = nodes[h->node].value.sound;
                int row_plus_one = stbds_hmget(rows, sound);
                if (!row_plus_one) {
                    row_plus_one = ++numrows;
                    if (numrows > 16) break;
                    stbds_hmput(rows, sound, row_plus_one);
                    int y = E->cursor_y - E->intscroll + row_plus_one;
                    if (y>=0 && y<TMH) {
                        int sound_idx = h->sound_idx;
                        const char *sound_name = sound ? sound->name : "";
                        print_to_screen(t.ptr, basex, y, 0x11188800, false, "%10s:%d ", sound_name, sound_idx);
                        print_to_screen(t.ptr, chartx + 128, y, 0x11188800, false, " %s:%d", sound_name, sound_idx);
                        for (int x=0;x<128;++x) {
                            uint32_t col = ((x&31)==0) ? C_CHART_HILITE : (x&8) ? C_CHART : C_CHART_HOVER;
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
                int hapx1 = (int)(h->t0 * 32.f);
                int hapx2 = (int)(h->t1 * 32.f);
                for (int x=hapx1;x<hapx2;++x) {
                    if (x>=0 && x<128) {
                        uint32_t charcol = hapcol ? hapcol : t.ptr[y * TMW + x + chartx] & 0xffffff00u;
                        t.ptr[y * TMW + x + chartx] = charcol | (unsigned char)((x==hapx1) ? 128+16+2 : (x==hapx2-1) ? '>' : 128+16+15);
                    }
                }
            }
            stbds_hmfree(rows);
            
        }
        if (cached_parser.err) {
            print_to_screen(t.ptr, basex + cached_parser.err, E->cursor_y + 1 - E->intscroll, C_ERR, false, cached_parser.errmsg);
        }
    }

    // draw a popup Above the cursor if its inside a curve
    if (cursor_in_curve_end_idx > cursor_in_curve_start_idx) {
        // trim the '
        cursor_in_curve_end_idx--;
        cursor_in_curve_start_idx++;
        int x, y;
        idx_to_xy(E, cursor_in_curve_start_idx, &x, &y);
        x += left;
        int datalen = cursor_in_curve_end_idx - cursor_in_curve_start_idx;
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
            value = clampf(value, 0.f, 64.f);
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
                    int y = E->cursor_y - 1 - j - E->intscroll;
                    if (y >= 0 && y < TMH) {
                        int ch = vertical_bar(datay);
                        t.ptr[y * TMW + xx] = ccol | (unsigned char)(ch); // draw a bar graph cellchar
                    }
                    datay -= 16;
                }
        } // graph drawing loop
    } // curve popup
    E->mouse_clicked_chart = false;

    return E->num_lines;
}


void load_file_into_editor(EditorState *E, bool init) {
    if (init) {
        E->font_width = 12;
        E->font_height = 24;
    }
    stbds_arrfree(E->str);
    E->str = load_file(E->fname);
}

#define RESW 1920
#define RESH 1080

void editor_update(EditorState *E, GLFWwindow *win) {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    mx *= retina;
    my *= retina;
    int m0 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    int m1 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (G) {
        G->mx = mx;
        G->my = my;
        G->old_mb = G->mb;
        G->mb = m0 + m1 * 2;
        G->iTime = glfwGetTime();
        G->cursor_x = E->cursor_x;
        G->cursor_y = E->cursor_y;
    }
    if (m0) {
        editor_click(E, G, mx, my, 1, 0);
        E->need_scroll_update = true;
    }

    int tmw = fbw / E->font_width;
    int tmh = fbh / E->font_height;
    if (tmw > 512)
        tmw = 512;
    if (tmh > 256 - 8)
        tmh = 256 - 8;

    E->scroll_y += (E->scroll_y_target - E->scroll_y) * 0.1;
    if (E->scroll_y < 0)
        E->scroll_y = 0;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[pbo_index]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    uint32_t *ptr = (uint32_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, textBytes,
                                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (ptr) {
        memset(ptr, 0, textBytes);
        E->intscroll = (int)(E->scroll_y / E->font_height);
        code_color(E, ptr);
        if (status_bar_time > glfwGetTime() - 3.0 && status_bar_color) {
            print_to_screen(ptr, 0, tmh, status_bar_color, false, status_bar);
        } else {
            status_bar_color = 0;
        }
        if (E->need_scroll_update) {
            E->need_scroll_update = false;
            if (E->cursor_y >= E->intscroll + tmh - 4) {
                E->scroll_y_target = (E->cursor_y - tmh + 4) * E->font_height;
            } else if (E->cursor_y < E->intscroll + 4) {
                E->scroll_y_target = (E->cursor_y - 4) * E->font_height;
            }
        }
        E->scroll_y_target = clampf(E->scroll_y_target, 0, (E->num_lines - tmh + 4) * E->font_height);
        // now find a zero crossing in the scope, and copy the relevant section
        // scope cant be more than 2048 as that's how many slots we have in the texture.
        uint32_t scope_start = scope_pos - 1024;
        uint32_t scan_max = 1024;
        int bestscani = 0;
        float bestscan = 0.f;
        for (int i = 1; i < scan_max; ++i) {
            float mono = stmid(scope[(scope_start - i) & SCOPE_MASK]);
            float mono_next = stmid(scope[(scope_start - i + 1) & SCOPE_MASK]);
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
            uint16_t l16 = (uint16_t)(clampf(sc.l, -1.f, 1.f) * 32767.f + 32768.f);
            uint16_t r16 = (uint16_t)(clampf(sc.r, -1.f, 1.f) * 32767.f + 32768.f);
            scope_dst[i] = (l16 >> 8) | (r16 & 0xff00);
        }
        scope_dst = ptr + (TMH - 20) * TMW;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int level =
                    xyscope[y * 2][x * 2] + xyscope[y * 2 + 1][x * 2] + xyscope[y * 2][x * 2 + 1] + xyscope[y * 2 + 1][x * 2 + 1];
                *scope_dst++ = level;
            }
        }

        static PFFFT_Setup *fft_setup = NULL;
        static float *fft_work = NULL;
        static float *fft_window = NULL;
        if (!fft_setup) {
            fft_setup = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
            fft_work = (float *)pffft_aligned_malloc(FFT_SIZE * 2 * sizeof(float));
            fft_window = (float *)pffft_aligned_malloc(FFT_SIZE * sizeof(float));

            const float a0 = 0.42f, a1 = 0.5f, a2 = 0.08f;
            const float scale = (float)(2.0 * M_PI) / (float)(FFT_SIZE - 1);
            for (int i = 0; i < FFT_SIZE; ++i) {
                float x = i * scale;
                fft_window[i] = a0 - a1 * cosf(x) + a2 * cosf(2.0f * x);
            }
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
            uint8_t l8 = (uint8_t)(clampf(255.f + magl_db * 3.f, 0.f, 255.f));
            uint8_t r8 = (uint8_t)(clampf(255.f + magr_db * 3.f, 0.f, 255.f));
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
}

void on_midi_input(uint8_t data[3], void *user) {
    if (!G)
        return;
    int cc = data[1];
    if (data[0] == 0xb0 && cc < 128) {
        int oldccdata = G->midi_cc[cc];
        int newccdata = data[2];
        G->midi_cc[cc] = newccdata;
        uint32_t gen = G->midi_cc_gen[cc]++;
        if (gen == 0)
            oldccdata = newccdata;
        if (newccdata != oldccdata && cc >= 16 && cc < 32 && closest_slider[cc - 16] != NULL) {
            // 'pickup': if we are increasing and bigger, or decreasing and smaller, then pick up the value, or closer than 2
            int sliderval = (int)clampf(closest_slider[cc - 16][0] * 127.f, 0.f, 127.f);
            int mindata = mini(oldccdata, newccdata);
            int maxdata = maxi(oldccdata, newccdata);
            int vel = newccdata - oldccdata;
            if (vel < 0)
                maxdata += (-vel) + 16;
            else
                mindata -= (vel) + 16; // add slop for velocity
            if (sliderval >= mindata - 4 && sliderval <= maxdata + 4) {
                closest_slider[cc - 16][0] = newccdata / 127.f;
            }
        }
    }
    // printf("midi: %02x %02x %02x\n", data[0], data[1], data[2]);
}

int main(int argc, char **argv) {
    printf("ginkgo - " __DATE__ " " __TIME__ "\n");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_sampler();
    //void test_minipat(void);
    //test_minipat();
    // return 0;
    
    int num_inputs = midi_get_num_inputs();
    int num_outputs = midi_get_num_outputs();
    printf("midi: %d inputs, %d outputs\n", num_inputs, num_outputs);
    int midi_input_idx = 0;
    for (int i = 0; i < num_inputs; ++i) {
        const char *name = midi_get_input_name(i);
        if (strstr(name, "Music Thing")) {
            midi_input_idx = i;
            break;
        }
    }
    for (int i = 0; i < num_inputs; ++i) {
        const char *name = midi_get_input_name(i);
        printf("input %d: %c%s\n", i, (i == midi_input_idx) ? '*' : ' ', name);
    }
    // for (int i = 0; i < num_outputs; ++i) {
    //     printf("output %d: %s\n", i, midi_get_output_name(i));
    // }
    midi_init(on_midi_input, NULL);
    midi_open_input(midi_input_idx);

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

    vs = compile_shader(NULL, GL_VERTEX_SHADER, kVS);
    fs = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS);
    prog = link_program(vs, fs);
    glDeleteShader(fs);

    load_file_into_editor(&shader_tab, true);
    load_file_into_editor(&audio_tab, true);
    try_to_compile_shader(&shader_tab);

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
    // stbi_uc *fontPixels = stbi_load("assets/font_recursive.png", &fw, &fh, &fc, 4);
    stbi_uc *fontPixels = stbi_load("assets/font_brutalita.png", &fw, &fh, &fc, 4);
    if (!fontPixels)
        die("Failed to load font");
    assert(fw == 32 * 16 && fh == 64 * 6);
    texFont = gl_create_texture(GL_LINEAR, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, fontPixels);
    stbi_image_free(fontPixels);
    check_gl("upload font");
    glBindTexture(GL_TEXTURE_2D, 0);

    texText = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
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

    glGenBuffers(3, pbos);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        glfwGetFramebufferSize(win, &fbw, &fbh);
        editor_update(curE, win);

        double iTime = glfwGetTime() - t0;
        ///////////// render the fpRT backbuffer (ie run the user shader)
        glDisable(GL_DEPTH_TEST);
        static uint32_t iFrame = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[iFrame % 2]);
        check_gl("bind fbo");
        glViewport(0, 0, RESW, RESH);
        if (!prog2) {
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glUseProgram(prog2); // a fragment shader that writes RGBA16F
            check_gl("use prog2");
            glUniform2i(loc_uScreenPx2, RESW, RESH);
            check_gl("uniform uScreenPx2");
            glUniform1ui(loc_iFrame2, iFrame);
            glUniform1f(loc_iTime2, iTime);
            check_gl("uniform iFrame");

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texFPRT[(iFrame + 1) % 2]);
            glUniform1i(loc_uFP2, 0);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            check_gl("draw fpRT");
        }

        ///////////// render the main backbuffer
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
        glUniform2i(loc_uFontPx, curE->font_width, curE->font_height);
        glUniform1i(loc_status_bar_size, status_bar_color ? 1 : 0);
        glUniform1f(loc_iTime, iTime);
        glUniform1f(loc_scroll_y, curE->scroll_y - curE->intscroll * curE->font_height);
        float f_cursor_x = curE->cursor_x * curE->font_width;
        float f_cursor_y = (curE->cursor_y - curE->intscroll) * curE->font_height;
        glUniform4f(loc_cursor, f_cursor_x, f_cursor_y, curE->prev_cursor_x, curE->prev_cursor_y);
        curE->prev_cursor_x += (f_cursor_x - curE->prev_cursor_x) * 0.2f;
        curE->prev_cursor_y += (f_cursor_y - curE->prev_cursor_y) * 0.2f;
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

        // poll for changes in the audio file
        static time_t last = 0;
        struct stat st;
        if (stat(audio_tab.fname, &st) == 0 && st.st_mtime != last) {
            last = st.st_mtime;
            try_to_compile_audio(audio_tab.fname, &audio_tab.last_compile_log);
            parse_error_log(&audio_tab);
        }

        // pump wave load requests
        Sound *bd = get_sound_for_main_thread("bd");
        get_wave(bd, 0);
        pump_wave_load_requests_main_thread();
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

    glfwDestroyWindow(win);
    glfwTerminate();
    curl_global_cleanup();
    return 0;
}
