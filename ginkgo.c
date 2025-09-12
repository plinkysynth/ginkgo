// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
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
#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "3rdparty/pffft.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers

#include "ginkgo.h"
#include "text_editor.h"
#include "audio_host.h"

char status_bar[512];
double status_bar_time = 0;
uint32_t status_bar_color = 0;

// size of virtual textmode screen:
#define TMW 512
#define TMH 256

// --- colors 3 digits bg, 3 digits fg, 2 digits character.
#define C_BOLD 0x80
#define C_SELECTION 0xc48fff00u
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
#define C_WARNING 0x860fff00u

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
        rendercol = max(vec3(0.), rendercol);
        float grey = dot(rendercol, vec3(0.2126, 0.7152, 0.0722));

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
        int thechar = (int(char_attrib.r & 127u)) - 32;
        vec4 fg = vec4(1.0), bg = vec4(1.0);
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
        float sdf_level = ((char_attrib.r & 128u)!=0u) ? 0.4f : 0.5f; // bold or not.
        float fontlvl = (thechar >= 0) ? sqrt(saturate((texture(uFont, fontpix).r-sdf_level) * aa + 0.5)) : 0.0;
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

EditorState audio_tab = {.fname="dsp.c", .is_shader=false};
EditorState shader_tab = {.fname="f.glsl", .is_shader=true};
EditorState *curE = &shader_tab;
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

static GLuint compile_shader(EditorState *E, GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (E) {
        free(E->last_compile_log);
        E->last_compile_log = NULL;
        hmfree(E->error_msgs);
        E->error_msgs = NULL;
    }
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        char *last_compile_log = (char *)malloc(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, NULL, last_compile_log);
        fprintf(stderr, "Shader compile failed:\n[%s]\n", last_compile_log);
        if (E) {
            E->last_compile_log = last_compile_log;
            for (const char *c = last_compile_log; *c; c++) {
                int col = 0, line = 0;
                if (sscanf(c, "ERROR: %d:%d:", &col, &line) == 2) {
                    hmput(E->error_msgs, line - 1, c);
                }
                while (*c && *c != '\n')
                    c++;
            }
            set_status_bar(C_ERR, "Shader compile failed - %d errors", (int)hmlen(E->error_msgs));
        } else {
            free(last_compile_log);
        }
        return 0;
    }
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

int fbw, fbh; // current framebuffer size in pixels
float retina = 1.0f;

static void adjust_font_size(EditorState *E, int delta) {
    float yzoom = E->cursor_y;
    E->scroll_y_target = E->scroll_y_target / E->font_height - yzoom;
    E->scroll_y = E->scroll_y / E->font_height - yzoom;
    E->font_width = clampi(E->font_width + delta, 8, 256);
    E->font_height = E->font_width * 2;
    E->scroll_y_target = (E->scroll_y_target + yzoom) * E->font_height;
    E->scroll_y = (E->scroll_y + yzoom) * E->font_height;
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
    EditorState *E = curE;
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (mods == 0) {
            if (key == GLFW_KEY_ESCAPE) {
                E->state.select_start = E->state.select_end = E->state.cursor;
            }

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

        if (key < 32 || key > 126 || (mods & GLFW_MOD_SUPER)) {
            if (mods == GLFW_MOD_SUPER) {
                if (key == GLFW_KEY_C || key == GLFW_KEY_X) {
                    int se = get_select_start(E);
                    int ee = get_select_end(E);
                    char *str = calloc(1, ee - se + 1);
                    memcpy(str, E->str + se, ee - se);
                    glfwSetClipboardString(win, str);
                    free(str);
                    if (key == GLFW_KEY_X) {
                        stb_textedit_cut(E, &E->state);
                    }
                }
                if (key == GLFW_KEY_V) {
                    const char *str = glfwGetClipboardString(win);
                    if (str) {
                        stb_textedit_paste(E, &E->state, (STB_TEXTEDIT_CHARTYPE *)str, strlen(str));
                    }
                }
                if (key == GLFW_KEY_SLASH) {
                    int n = stbds_arrlen(E->str);
                    int ss = get_select_start(E);
                    int se = get_select_end(E);
                    if (ss == se)
                        ss = se = E->state.cursor;
                    int first_line = find_line_index(E->str, n, ss);
                    int last_line = find_line_index(E->str, n, se);
                    int y = 0;
                    for (int i = 0; i < arrlen(E->str); i++) {
                        int n = stbds_arrlen(E->str);
                        if (y >= first_line && y <= last_line) {
                            while (i < n && E->str[i] != '\n' && E->str[i] != 0 && isspace(E->str[i]))
                                i++;
                            // if it starts with //, remove it. otherwise add it.
                            if (i + 1 < n && E->str[i] == '/' && E->str[i + 1] == '/') {
                                arrdeln(E->str, i, 2);
                                if (i < n - 2 && E->str[i] == ' ')
                                    arrdel(E->str, i);
                            } else {
                                arrinsn(E->str, i, 3);
                                E->str[i] = '/';
                                E->str[i + 1] = '/';
                                E->str[i + 2] = ' ';
                            }
                        }
                        n = stbds_arrlen(E->str);
                        while (i < n && E->str[i] != '\n' && E->str[i] != 0)
                            i++;
                        y++;
                    }
                }
                if (key == GLFW_KEY_A) {
                    E->state.select_start = 0;
                    E->state.select_end = stbds_arrlen(E->str);
                }
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
                if (key == GLFW_KEY_MINUS) {
                    adjust_font_size(E, -1);
                }
                if (key == GLFW_KEY_EQUAL) {
                    adjust_font_size(E,1);
                }
            }
            // printf("key: %d, mods: %d\n", key, mods);
            stb_textedit_key(E, &E->state, key | (mods << 16));
            // printf("cursor %d , select_start %d, select_end %d\n", state.cursor, state.select_start, state.select_end);
            E->need_scroll_update = true;
        }
    }
}

static void scroll_callback(GLFWwindow *win, double xoffset, double yoffset) {
    // printf("scroll: %f\n", yoffset);
    curE->scroll_y_target -= yoffset * curE->font_height;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    // printf("char: %d\n", codepoint);
    stb_textedit_key(curE, &curE->state, codepoint);
    curE->need_scroll_update = true;
}

static void mouse_button_callback(GLFWwindow *win, int button, int action, int mods) {
    if (action == GLFW_PRESS) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
        stb_textedit_click(curE, &curE->state, mx - 64., my + curE->scroll_y);
        curE->need_scroll_update = true;
    }
    if (action == GLFW_RELEASE) {
        // printf("mouse button: %d, mods: %d\n", button, mods);
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        mx *= retina;
        my *= retina;
        stb_textedit_drag(curE, &curE->state, mx - 64., my + curE->scroll_y);
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
        return invert_color(C_PUN) | C_BOLD;
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
                t->ptr[oy * TMW + ox] = invert_color(char_and_col) | C_BOLD;
                return invert_color(C_PUN) | C_BOLD;
            }
        }
        if (ox == t->cursor_x - 1 && oy == t->cursor_y) {
            // over the opening bracket. hilight this closing bracket
            return invert_color(C_PUN) | C_BOLD;
        }
        return C_PUN;
    }
    return C_ERR;
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
    for (int i = 0; i <= t.n + 10;) {
        unsigned h = 0;
        char c = tok_get(&t, i);
        uint32_t col = C_DEF;
        int j = i + 1;
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
#define CASE(x) case HASH(x)
#define CASE2(x, y)                                                                                                                \
    case HASH(x):                                                                                                                  \
    case HASH(y)
#define CASE4(x, y, z, w) CASE2(x, y) : CASE2(z, w)
#define CASE8(a, b, c, d, e, f, g, h) CASE4(a, b, c, d) : CASE4(e, f, g, h)
            if (is_ident_start((unsigned)c)) {
                col = C_DEF;
                j = scan_ident(t.str, i, t.n);
                h = hash_span(t.str + i, t.str + j);
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
            uint32_t ccol = col;
            if (i >= se && i < ee)
                ccol = C_SELECTION;

            if (i == E->state.cursor) {
                E->cursor_x = t.x;
                E->cursor_y = t.y + E->intscroll;
            }
            if (t.x < TMW && t.y >= 0 && t.y < TMH)
                t.ptr[t.y * TMW + t.x] = (ccol) | (unsigned char)(ch);
            if (ch == '\t')
                t.x += 2;
            else if (ch == '\n' || ch == 0) {
                // look for an error message
                const char *errline = hmget(E->error_msgs, t.y);
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
    E->num_lines = t.y + 1 + E->intscroll;
    return E->num_lines;
}

void load_file(EditorState *E, bool init) {
    if (init) {
        E->font_width = 12;
        E->font_height = 24;
        stb_textedit_initialize_state(&E->state, 0);
    }
    FILE *f = fopen(E->fname, "r");
    if (!f) {
        stbds_arrfreef(E->str);
        E->str = NULL;
        return;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    stbds_arrsetlen(E->str, len);
    fseek(f, 0, SEEK_SET);
    fread(E->str, 1, len, f);
    fclose(f);
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
    if (m0) {
        stb_textedit_drag(E, &E->state, mx - 64., my + E->scroll_y);
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
            int x = 0;
            for (const char *c = status_bar; *c && *c != '\n' && x < TMW; c++, x++) {
                ptr[tmh * TMW + x] = (status_bar_color) | (unsigned char)(*c);
            }
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
            uint16_t l16 = (uint16_t)(clampf(sc.l, -1.f, 1.f) * 32767.f + 32768.f);
            uint16_t r16 = (uint16_t)(clampf(sc.r, -1.f, 1.f) * 32767.f + 32768.f);
            scope_dst[i] = (l16 >> 8) | (r16 & 0xff00);
        }
        scope_dst = ptr + (TMH - 20) * TMW;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                int level = xyscope[y * 2][x * 2] + xyscope[y * 2 + 1][x * 2] + xyscope[y * 2][x * 2 + 1] +
                            xyscope[y * 2 + 1][x * 2 + 1];
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
            for (int i = 0; i < FFT_SIZE; ++i) {
                fft_window[i] = 0.5f - 0.5f * cosf(2.f * M_PI * i / FFT_SIZE);
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

}

int main(int argc, char **argv) {
    printf("ginkgo - " __DATE__ " " __TIME__ "\n");
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

    load_file(&shader_tab, true);
    load_file(&audio_tab, true);
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
    stbi_uc *fontPixels = stbi_load("assets/font_recursive.png", &fw, &fh, &fc, 4);
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
            kick_compile(audio_tab.fname);
        }
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
