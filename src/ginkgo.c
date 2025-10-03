
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
#include "ansicols.h"
#include "utils.h"
#include "ginkgo.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers
#include "audio_host.h"
#include "midi_mac.h"
#include "hash_literal.h"
#include "miniparse.h"
#include "sampler.h"
#include "text_editor.h"

char status_bar[512];
double status_bar_time = 0;
uint32_t status_bar_color = 0;



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

void parse_error_log(EditorState *E);

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
    // printf("retina: %f\n", retina);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    return win;
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
    G->mx = mx;
    G->my = my;
    G->old_mb = G->mb;
    G->mb = m0 + m1 * 2;
    G->iTime = glfwGetTime();
    G->cursor_x = E->cursor_x;
    G->cursor_y = E->cursor_y;
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
    // void test_minipat(void);
    // test_minipat();
    //  return 0;

    int num_inputs = midi_get_num_inputs();
    int num_outputs = midi_get_num_outputs();
    printf("midi: " COLOR_GREEN "%d" COLOR_RESET " inputs, " COLOR_GREEN "%d" COLOR_RESET " outputs\n", num_inputs, num_outputs);
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
        printf("input %d: %c" COLOR_YELLOW "%s" COLOR_RESET "\n", i, (i == midi_input_idx) ? '*' : ' ', name);
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
