
// clang -std=c11 -O2 gpu.c -o gpu -I$(brew --prefix glfw)/include -L$(brew --prefix glfw)/lib -lglfw -framework OpenGL -framework
// Cocoa -framework IOKit -framework CoreVideo
#define STB_IMAGE_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#define MINIAUDIO_IMPLEMENTATION
#include <stdio.h>
#include <stdatomic.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <curl/curl.h>
#include "3rdparty/stb_image.h"
#include "3rdparty/stb_ds.h"
#include "3rdparty/miniaudio.h"
#include "3rdparty/pffft.h"
#include "ansicols.h"
#include "ginkgo.h"
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h> // core profile headers
#include "audio_host.h"
#include "midi_mac.h"
#include "sampler.h"
#include "text_editor.h"
#include "svf_gain.h"
#define RESW 1920
#define RESH 1080



// Named constants for magic numbers
#define BLOOM_FADE_FACTOR (1.f / 16.f)
#define BLOOM_SPIKEYNESS 0.5f
#define BLOOM_KERNEL_SIZE_DOWNSAMPLE 1.5f
#define BLOOM_KERNEL_SIZE_UPSAMPLE 3.f
#define CURSOR_SMOOTH_FACTOR 0.2f

// Status bar system is defined in text_editor.h

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

typedef struct line_t {
    float     p0x,p0y;  
    float     p1x,p1y;
    uint32_t  col;  
    float     width;
} line_t; 

#define  MAX_LINES (1<<15)
uint32_t line_count = 0;
line_t lines[MAX_LINES];

void add_line(float p0x, float p0y, float p1x, float p1y, uint32_t col, float width) {
    if (line_count >= MAX_LINES) return;
    lines[line_count] = (line_t){p0x,p0y,p1x,p1y,col,width};
    line_count++;
}


static void bind_texture_to_slot(GLuint shader, int slot, const char *uniform_name, GLuint texture, GLenum mag_filter) {
    glUniform1i(glGetUniformLocation(shader, uniform_name), slot);
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
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

static void setup_framebuffers_and_textures(GLuint *texFPRT, GLuint *fbo, int num_bloom_mips) {
    for (int i = 0; i < 2 + num_bloom_mips; ++i) {
        glGenFramebuffers(1, &fbo[i]);
        texFPRT[i] = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, texFPRT[i]);
        int resw = (i < 2) ? RESW : (RESW >> (i - 1));
        int resh = (i < 2) ? RESH : (RESH >> (i - 1));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, resw, resh, 0, GL_RGBA, GL_FLOAT, NULL);
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
}

static void draw_fullscreen_pass(GLuint fbo, int viewport_w, int viewport_h, GLuint vao) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, viewport_w, viewport_h);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    check_gl("draw fullscreen pass");
}

static void set_bloom_uniforms(GLuint shader, int resw, int resh, float kernel_size, float fade_factor) {
    glUniform2f(glGetUniformLocation(shader, "duv"), kernel_size / resw, kernel_size / resh);
    glUniform1f(glGetUniformLocation(shader, "fade"), fade_factor);
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

const char *kVS_fat = SHADER(
    layout(location=0) in vec2 in_p1;
    layout(location=1) in vec2 in_p2;
    layout(location=2) in float in_width;
    layout(location=3) in vec4  in_col; // from normalized ubyte4
    out vec2 v_uv;
    out vec4 v_col;
    out vec2 v_width_len;
    uniform vec2 fScreenPx;
    void main() {
        vec2 p1 = in_p1, p2 = in_p2;
        float hw = 0.5 * in_width;
        vec2 dir = p2-p1;
        float len = length(dir);
        vec2 t = (len<1e-6) ? vec2(hw, 0.0) : dir * -(hw/len);
        vec2 n = vec2(-t.y, t.x);
        // 0    1/5
        // 2/4    3
        int corner = gl_VertexID % 6;
        vec2 p = p1;
        vec2 uv = vec2(-hw,-hw);
        if ((corner&1)==1) { t=-t; p = p2; uv.y = len+hw;}
        if (corner>=2 && corner<=4) { n=-n; uv.x=-uv.x;}
        p=p+t+n;
        v_uv  = uv;
        v_col = in_col;
        v_width_len = vec2(hw, len);
        p*=2.f/fScreenPx;
        p-=1.f;
        p.y=-p.y;
        gl_Position = vec4(p, 0.0, 1.0);
        });

const char *kFS_fat = SHADER(
    float saturate(float x) { return clamp(x, 0.0, 1.0); }

    out vec4 o_color; 
    in vec2 v_uv; 
    in vec4 v_col;
    in vec2 v_width_len;
    void main() {
        o_color = v_col;
        vec2 uv = v_uv;
        if (uv.y>0.) uv.y = max(0., uv.y - v_width_len.y);
        float sdf = v_width_len.x-length(uv);
        o_color=v_col * saturate(sdf);
    });

const char *kVS = SHADER(
out vec2 v_uv; 
uniform ivec2 uScreenPx;

void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p;
    gl_Position = vec4(p.x * 2.0 - 1.0, p.y * 2.0 - 1.0, 0.0, 1.0);
});

const char *kFS_user_prefix = SHADER(
out vec4 o_color; 
in vec2 v_uv; 
uniform uint iFrame;
uniform float iTime; 
uint seed; 
uniform sampler2D uFP; 
uniform ivec2 uScreenPx;
uniform sampler2D uFont; 
uniform usampler2D uText; 
float square(float x) { return x * x; }
vec2 wave_read(float x, int y_base) {
    int ix = int(x);
    vec2 s0 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    ix++;
    vec2 s1 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    return mix(s0, s1, fract(x));
}
vec2 scope(float x) { return (wave_read(x, 256 - 4) - 128.f) * (1.f/128.f); }

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
    
const char *kFS_user_suffix = SHADER_NO_VERSION(
   return o; } // end of user shader
    void main() {
        vec2 fragCoord = gl_FragCoord.xy;
        seed = uint(uint(fragCoord.x) * uint(1973) + uint(fragCoord.y) * uint(9277) + uint(iFrame) * uint(23952683)) | uint(1);
        o_color = vec4(c(v_uv), 1.0);
    });

const char *kFS_bloom = SHADER(
    out vec4 o_color; 
    in vec2 v_uv; 
    uniform sampler2D uFP; 
    uniform vec2 duv;
    uniform float fade;
    void main() {
        
        vec4 c0 = texture(uFP, v_uv + vec2(-duv.x, -duv.y));
        c0     += texture(uFP, v_uv + vec2(     0, -duv.y)) * 2.;
        c0     += texture(uFP, v_uv + vec2( duv.x, -duv.y));
        c0     += texture(uFP, v_uv + vec2(-duv.x,      0)) * 2.;
        c0     += texture(uFP, v_uv + vec2(     0,      0)) * 4.;
        c0     += texture(uFP, v_uv + vec2( duv.x,      0)) * 2.;
        c0     += texture(uFP, v_uv + vec2(-duv.x,  duv.y));
        c0     += texture(uFP, v_uv + vec2(     0,  duv.y)) * 2.;
        c0     += texture(uFP, v_uv + vec2( duv.x,  duv.y));
        o_color = c0 * fade;
    }
); 

const char *kFS_ui = SHADER(
    float saturate(float x) { return clamp(x, 0.0, 1.0); }
    uniform ivec2 uScreenPx; 
    uniform float iTime; 
    uniform float scroll_y; 
    uniform vec4 cursor; 
    uniform float ui_alpha;
    out vec4 o_color; 
    in vec2 v_uv;

    uniform ivec2 uFontPx;
    uniform int status_bar_size;
    uniform sampler2D uFP; 
    uniform sampler2D uFont; 
    uniform usampler2D uText; 
    uniform sampler2D uBloom;

    vec3 aces(vec3 x) {
        const float a=2.51;
        const float b=0.03;
        const float c=2.43;
        const float d=0.59;
        const float e=0.14;
        return clamp((x*(a*x+b)) / (x*(c*x+d)+e), 0.0, 1.0);
    }
    vec2 wave_read(float x, int y_base) {
        int ix = int(x);
        vec2 s0 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
        ix++;
        vec2 s1 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
        return mix(s0, s1, fract(x));
    }

    vec2 scope(float x) { return (wave_read(x, 256 - 4) - 128.f) * 0.25; }

    void main() {
        vec3 rendercol= texture(uFP, v_uv).rgb;
        vec3 bloomcol= texture(uBloom, v_uv).rgb;
        rendercol += bloomcol * 0.5;
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
                pix.y = (96.f / 48000.f * 4096.f) * pow(500.f, v_uv.y + dither); // 24hz to nyquist
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
            rendercol += max(vec3(0.),beamcol);
        }
        rendercol.rgb = sqrt(aces(rendercol.rgb));

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
        fontcol*=ui_alpha;
        o_color = vec4(rendercol.xyz * (1.-fontcol.w) + fontcol.xyz, 1.0);
    });
// clang-format on

EditorState audio_tab = {.fname = "livesrc/audio.cpp", .is_shader = false};
EditorState shader_tab = {.fname = "livesrc/video.glsl", .is_shader = true};
EditorState *curE = &audio_tab;
float ui_alpha = 0.f;
float ui_alpha_target = 1.f;
size_t textBytes = (size_t)(TMW * TMH * 4);
static float retina = 1.0f;

GLuint ui_pass = 0, user_pass = 0, bloom_pass = 0;
GLuint vs = 0, fs_ui = 0, fs_bloom = 0;
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
    char *compile_log = NULL;
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        stbds_arrsetlen(compile_log, len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, NULL, compile_log);
        fprintf(stderr, "Shader compile failed:\n[%s]\n", compile_log);
    }
    if (E) {
        stbds_arrfree(E->last_compile_log);
        E->last_compile_log = compile_log;
        stbds_hmfree(E->error_msgs);
        E->error_msgs = NULL;
        if (!ok) {
            parse_error_log(E);
            set_status_bar(C_ERR, "Shader compile failed - %d errors", (int)hmlen(E->error_msgs));
        }
    } else {
        stbds_arrfree(compile_log);
    }
    if (!ok)
        return 0;
    return s;
}

GLuint try_to_compile_shader(EditorState *E) {
    if (!E->is_shader) {
        return 0;
    }
    int len = strlen(kFS_user_prefix) + stbds_arrlen(E->str) + strlen(kFS_user_suffix) + 64;
    char *fs_user_str = (char *)calloc(1, len);
    snprintf(fs_user_str, len, "%s\n#line 1\n%.*s\n%s", kFS_user_prefix, (int)stbds_arrlen(E->str), E->str, kFS_user_suffix);
    GLuint fs_user = compile_shader(E, GL_FRAGMENT_SHADER, fs_user_str);
    GLuint new_user_pass = fs_user ? link_program(vs, fs_user) : 0;
    if (new_user_pass) {
        glDeleteProgram(user_pass);
        user_pass = new_user_pass;
    }
    if (!user_pass)
        die("Failed to compile shader");
    glDeleteShader(fs_user);
    free(fs_user_str);
    GLenum e = glGetError(); // clear any errors
    return new_user_pass;
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
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    return win;
}

static void set_tab(EditorState *newE) {
    ui_alpha_target = (ui_alpha_target>0.5 && curE == newE) ? 0.f : 1.f;
    curE = newE;
}

static void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods) {
    EditorState *E = curE;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    if (mods == 0) {

        if (key == GLFW_KEY_F1) {
            set_tab(&shader_tab);
        }
        if (key == GLFW_KEY_F2) {
            set_tab(&audio_tab);
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
                        set_status_bar(C_OK, E->is_shader ? "saved shader" : "saved audio");
                        if (!E->is_shader)
                            parse_named_patterns_in_c_source(E->str, E->str + stbds_arrlen(E->str));
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
            else
                parse_named_patterns_in_c_source(E->str, E->str + stbds_arrlen(E->str));
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
            int x = tmw - strlen(status_bar);
            print_to_screen(ptr, x, tmh, status_bar_color, false, "%s", status_bar);
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
        E->scroll_y_target = clamp(E->scroll_y_target, 0.f, float((E->num_lines - tmh + 4) * E->font_height));
        // now find a zero crossing in the scope, and copy the relevant section
        // scope cant be more than 2048 as that's how many slots we have in the texture.
        uint32_t scope_start = scope_pos - 1024;
        uint32_t scan_max = 1024;
        int bestscani = 0;
        float bestscan = 0.f;
        for (int i = 1; i < scan_max; ++i) {
            float mono = stmid(scope[(scope_start - i) & SCOPE_MASK]);
            float mono_next = stmid(scope[(scope_start - i + 1) & SCOPE_MASK]);
            float delta = mono - mono_next;
            if (mono > 0.f && mono_next < 0.f && delta > bestscan) {
                bestscan = delta;
                bestscani = i;
                break;
            }
        }
        scope_start -= 1024 + bestscani;
        uint32_t *scope_dst = ptr + (TMH - 4) * TMW;
        for (int i = 0; i < 2048; ++i) {
            stereo sc = scope[(scope_start + i) & SCOPE_MASK];
            uint16_t l16 = (uint16_t)(clamp(sc.l, -1.f, 1.f) * 32767.f + 32768.f);
            uint16_t r16 = (uint16_t)(clamp(sc.r, -1.f, 1.f) * 32767.f + 32768.f);
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
        float fft_buf[3][FFT_SIZE];
        for (int i = 0; i < FFT_SIZE; ++i) {
            stereo sc = scope[(fft_start + i) & SCOPE_MASK];
            stereo pr = probe_scope[(fft_start + i) & SCOPE_MASK];
            fft_buf[0][i] = sc.l * fft_window[i];
            fft_buf[1][i] = sc.r * fft_window[i];
            fft_buf[2][i] = (pr.l+pr.r) * fft_window[i];
        }
        pffft_transform_ordered(fft_setup, fft_buf[0], fft_buf[0], fft_work, PFFFT_FORWARD);
        pffft_transform_ordered(fft_setup, fft_buf[1], fft_buf[1], fft_work, PFFFT_FORWARD);
        pffft_transform_ordered(fft_setup, fft_buf[2], fft_buf[2], fft_work, PFFFT_FORWARD);
        scope_dst = ptr + (TMH - 12) * TMW;
        float peak_mag = squared2db(square(0.25f * FFT_SIZE)); // assuming hann, coherent gain is 0.5
        // int min=0, max=0;
        // float minv=1000.f, maxv=-1000.f;
        for (int i = 0; i < 4096; ++i) {
            float magl_db =
                squared2db(fft_buf[0][i * 2] * fft_buf[0][i * 2] + fft_buf[0][i * 2 + 1] * fft_buf[0][i * 2 + 1]) - peak_mag;
            float magr_db =
                squared2db(fft_buf[1][i * 2] * fft_buf[1][i * 2] + fft_buf[1][i * 2 + 1] * fft_buf[1][i * 2 + 1]) - peak_mag;
            float probe_db =
                squared2db(fft_buf[2][i * 2] * fft_buf[2][i * 2] + fft_buf[2][i * 2 + 1] * fft_buf[2][i * 2 + 1]) - peak_mag + 6.f;
            probe_db_smooth[i] = probe_db_smooth[i] + (probe_db - probe_db_smooth[i]) * 0.05f;
            // if (magl_db < minv) {minv = magl_db;min = i;}
            // if (magl_db > maxv) {maxv = magl_db;max = i;}
            uint8_t l8 = (uint8_t)(clamp(255.f + magl_db * 6.f, 0.f, 255.f));
            uint8_t r8 = (uint8_t)(clamp(255.f + magr_db * 6.f, 0.f, 255.f));
            scope_dst[i] = (l8 << 0) | (r8 << 8);
        }
        // printf("fft min: %f in bin %d, max: %f in bin %d\n", minv, min, maxv, max);

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
            int sliderval = (int)clamp(closest_slider[cc - 16][0] * 127.f, 0.f, 127.f);
            int mindata = min(oldccdata, newccdata);
            int maxdata = max(oldccdata, newccdata);
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

// Audio and MIDI initialization
static void init_audio_midi(ma_device *dev) {
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

    midi_init(on_midi_input, NULL);
    midi_open_input(midi_input_idx);

    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = SAMPLE_RATE_OUTPUT;
    cfg.capture.format = cfg.playback.format = ma_format_f32;
    cfg.capture.channels = cfg.playback.channels = 2;
    cfg.dataCallback = audio_cb;

    if (ma_device_init(NULL, &cfg, dev) != MA_SUCCESS) {
        die("ma_device_init failed");
    }
    if (ma_device_start(dev) != MA_SUCCESS) {
        fprintf(stderr, "ma_device_start failed\n");
        ma_device_uninit(dev);
        exit(3);
    }
}

// Render pass functions
static void render_user_pass(GLuint user_pass, GLuint *fbo, GLuint *texFPRT, uint32_t iFrame, double iTime, GLuint vao,
                             GLuint texFont, GLuint texText) {
    if (!user_pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[iFrame % 2]);
        glViewport(0, 0, RESW, RESH);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        glUseProgram(user_pass);
        glUniform2i(glGetUniformLocation(user_pass, "uScreenPx"), RESW, RESH);
        glUniform1f(glGetUniformLocation(user_pass, "iTime"), (float)iTime);
        glUniform1ui(glGetUniformLocation(user_pass, "iFrame"), iFrame);
        bind_texture_to_slot(user_pass, 1, "uFont", texFont, GL_LINEAR);
        bind_texture_to_slot(user_pass, 2, "uText", texText, GL_NEAREST);
        bind_texture_to_slot(user_pass, 0, "uFP", texFPRT[(iFrame + 1) % 2], GL_NEAREST);
        draw_fullscreen_pass(fbo[iFrame % 2], RESW, RESH, vao);
    }
}

static void render_bloom_downsample(GLuint bloom_pass, GLuint *fbo, GLuint *texFPRT, uint32_t iFrame, int num_bloom_mips,
                                    GLuint vao) {
    for (int i = 0; i < num_bloom_mips; ++i) {
        int resw = RESW >> (i + 1);
        int resh = RESH >> (i + 1);
        glUseProgram(bloom_pass);
        set_bloom_uniforms(bloom_pass, resw, resh, BLOOM_KERNEL_SIZE_DOWNSAMPLE, BLOOM_FADE_FACTOR);
        bind_texture_to_slot(bloom_pass, 0, "uFP", texFPRT[i ? (i + 1) : (iFrame % 2)], GL_LINEAR);
        draw_fullscreen_pass(fbo[i + 2], resw, resh, vao);
    }
}

static void render_bloom_upsample(GLuint bloom_pass, GLuint *fbo, GLuint *texFPRT, int num_bloom_mips, GLuint vao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBlendFunc(GL_DST_ALPHA, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);

    for (int i = num_bloom_mips - 2; i >= 0; --i) {
        int resw = RESW >> (i + 1);
        int resh = RESH >> (i + 1);
        glUseProgram(bloom_pass);
        set_bloom_uniforms(bloom_pass, resw, resh, BLOOM_KERNEL_SIZE_UPSAMPLE, BLOOM_FADE_FACTOR * BLOOM_SPIKEYNESS);
        bind_texture_to_slot(bloom_pass, 0, "uFP", texFPRT[i + 2 + 1], GL_LINEAR);
        draw_fullscreen_pass(fbo[i + 2], resw, resh, vao);
    }
    glDisable(GL_BLEND);
}

static void render_ui_pass(GLuint ui_pass, GLuint *texFPRT, GLuint texFont, GLuint texText, uint32_t iFrame, EditorState *curE,
                           int fbw, int fbh, double iTime, GLuint vao, float ui_alpha) {
    glUseProgram(ui_pass);
    bind_texture_to_slot(ui_pass, 0, "uFP", texFPRT[iFrame % 2], GL_LINEAR);
    bind_texture_to_slot(ui_pass, 1, "uFont", texFont, GL_LINEAR);
    bind_texture_to_slot(ui_pass, 2, "uText", texText, GL_NEAREST);
    bind_texture_to_slot(ui_pass, 3, "uBloom", texFPRT[2], GL_LINEAR);

    glUniform2i(glGetUniformLocation(ui_pass, "uScreenPx"), fbw, fbh);
    glUniform2i(glGetUniformLocation(ui_pass, "uFontPx"), curE->font_width, curE->font_height);
    glUniform1i(glGetUniformLocation(ui_pass, "status_bar_size"), status_bar_color ? 1 : 0);
    glUniform1f(glGetUniformLocation(ui_pass, "iTime"), (float)iTime);
    glUniform1f(glGetUniformLocation(ui_pass, "scroll_y"), curE->scroll_y - curE->intscroll * curE->font_height);
    glUniform1f(glGetUniformLocation(ui_pass, "ui_alpha"), ui_alpha);

    float f_cursor_x = curE->cursor_x * curE->font_width;
    float f_cursor_y = (curE->cursor_y - curE->intscroll) * curE->font_height;
    glUniform4f(glGetUniformLocation(ui_pass, "cursor"), f_cursor_x, f_cursor_y, curE->prev_cursor_x, curE->prev_cursor_y);
    curE->prev_cursor_x += (f_cursor_x - curE->prev_cursor_x) * CURSOR_SMOOTH_FACTOR;
    curE->prev_cursor_y += (f_cursor_y - curE->prev_cursor_y) * CURSOR_SMOOTH_FACTOR;

    draw_fullscreen_pass(0, fbw, fbh, vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int main(int argc, char **argv) {
    printf(COLOR_CYAN "ginkgo" COLOR_RESET " - " __DATE__ " " __TIME__ "\n");

    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_sampler();

    // void test_minipat(void);
    // test_minipat();
    // return 0;

    ma_device dev;
    init_audio_midi(&dev);

    int want_fullscreen = (argc > 1 && (strcmp(argv[1], "--fullscreen") == 0 || strcmp(argv[1], "-f") == 0));
    GLFWwindow *win = gl_init(want_fullscreen);

    vs = compile_shader(NULL, GL_VERTEX_SHADER, kVS);
    fs_ui = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_ui);
    fs_bloom = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_bloom);
    ui_pass = link_program(vs, fs_ui);
    bloom_pass = link_program(vs, fs_bloom);
    glDeleteShader(fs_ui);
    glDeleteShader(fs_bloom);
    GLuint vs_fat = compile_shader(NULL, GL_VERTEX_SHADER, kVS_fat);
    GLuint fs_fat = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_fat);
    GLuint fat_prog = link_program(vs_fat, fs_fat);
    glDeleteShader(fs_fat);
    glDeleteShader(vs_fat);

    load_file_into_editor(&shader_tab, true);
    load_file_into_editor(&audio_tab, true);
    try_to_compile_shader(&shader_tab);
    parse_named_patterns_in_c_source(audio_tab.str, audio_tab.str + stbds_arrlen(audio_tab.str));

    int fw = 0, fh = 0, fc = 0;
    // stbi_uc *fontPixels = stbi_load("assets/font_recursive.png", &fw, &fh, &fc, 4);
    // stbi_uc *fontPixels = stbi_load("assets/font_brutalita.png", &fw, &fh, &fc, 4);
    stbi_uc *fontPixels = stbi_load("assets/font_sdf.png", &fw, &fh, &fc, 4);
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

#define NUM_BLOOM_MIPS 3
    GLuint texFPRT[2 + NUM_BLOOM_MIPS];
    GLuint fbo[2 + NUM_BLOOM_MIPS];
    setup_framebuffers_and_textures(texFPRT, fbo, NUM_BLOOM_MIPS);

    glGenBuffers(3, pbos);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    // fat line init
    GLuint fatvao[2]={}, fatvbo[2]={};
    glGenVertexArrays(2,fatvao);
    glGenBuffers(2,fatvbo);

    GLsizeiptr cap_bytes = (GLsizeiptr)MAX_LINES * sizeof(line_t);

    for (int i=0;i<2;i++) {
        glBindVertexArray(fatvao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, fatvbo[i]);
        glBufferData(GL_ARRAY_BUFFER, cap_bytes, NULL, GL_DYNAMIC_DRAW); // pre-alloc

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(line_t), (void*)offsetof(line_t,p0x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(line_t), (void*)offsetof(line_t,p1x));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(line_t), (void*)offsetof(line_t,width));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(line_t), (void*)offsetof(line_t,col));
        glVertexAttribDivisor(0,1);
        glVertexAttribDivisor(1,1);
        glVertexAttribDivisor(2,1);
        glVertexAttribDivisor(3,1);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }


    double t0 = glfwGetTime();
    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        glfwGetFramebufferSize(win, &fbw, &fbh);

        double iTime = glfwGetTime() - t0;
        static uint32_t iFrame = 0;

        glDisable(GL_DEPTH_TEST);
        editor_update(curE, win);
        render_user_pass(user_pass, fbo, texFPRT, iFrame, iTime, vao, texFont, texText);
        render_bloom_downsample(bloom_pass, fbo, texFPRT, iFrame, NUM_BLOOM_MIPS, vao);
        render_bloom_upsample(bloom_pass, fbo, texFPRT, NUM_BLOOM_MIPS, vao);
        render_ui_pass(ui_pass, texFPRT, texFont, texText, iFrame, curE, fbw, fbh, iTime, vao, ui_alpha);

        #define MOUSE_LEN 8
        #define MOUSE_MASK (MOUSE_LEN-1)
        static float mxhistory[MOUSE_LEN], myhistory[MOUSE_LEN];
        int mpos = (iFrame % MOUSE_LEN);
        mxhistory[mpos] = G->mx;
        myhistory[mpos] = G->my;
        int numlines = min(iFrame, MOUSE_LEN-1u);
        for (int i=0;i<numlines;i++) {
            float p0x = mxhistory[(mpos-i)&MOUSE_MASK];
            float p0y = myhistory[(mpos-i)&MOUSE_MASK];
            float p1x = mxhistory[(mpos-i-1)&MOUSE_MASK];
            float p1y = myhistory[(mpos-i-1)&MOUSE_MASK];
            float len = 50.f + sqrtf(square(p0x-p1x) + square(p0y-p1y));
            int alpha = (int)clamp(len,0.f,255.f);
            // red is in the lsb. premultiplied alpha so alpha=0 == additive
            uint32_t col = (alpha << 24) | ((alpha>>0)<<16) | ((alpha>>0)<<8) | (alpha>>0);
            if (i==0) col=0;
            add_line(p0x, p0y, p1x, p1y, col, 17.f-i);
        }
        //test_svf_gain();

        // fat line draw
        if (line_count > 0) {
            //line_t lines[1] = {{300.f,100.f,G->mx,G->my,0xffff00ff,54.f}};
            GLsizeiptr bytes = (GLsizeiptr)((line_count <= MAX_LINES ? line_count : MAX_LINES) * sizeof(line_t));

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glBindVertexArray(fatvao[iFrame % 2]);
            glBindBuffer(GL_ARRAY_BUFFER, fatvbo[iFrame % 2]);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, lines);
            glUseProgram(fat_prog);
            glUniform2f(glGetUniformLocation(fat_prog, "fScreenPx"), fbw, fbh);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, line_count);
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDisable(GL_BLEND);
            line_count = 0;
        }

        ui_alpha += (ui_alpha_target - ui_alpha) * 0.1;

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

        double t = G->t;
        int bar = (int)t;
        int beat = (int)((t - bar) * 64);
        set_status_bar(0x04cfff00, " %.1f%% | %x.%02x ", G->cpu_usage_smooth*100.f, bar, beat);

        // pump wave load requests
        pump_wave_load_requests_main_thread();
    }
    ma_device_stop(&dev);
    ma_device_uninit(&dev);

    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texText);
    glDeleteTextures(1, &texFont);
    glDeleteTextures(2 + NUM_BLOOM_MIPS, texFPRT);
    glDeleteFramebuffers(2 + NUM_BLOOM_MIPS, fbo);
    glDeleteProgram(ui_pass);
    glDeleteBuffers(3, pbos);

    glfwDestroyWindow(win);
    glfwTerminate();
    curl_global_cleanup();
    return 0;
}
