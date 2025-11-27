#define STB_IMAGE_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION
#define GLFW_INCLUDE_NONE
#define MINIAUDIO_IMPLEMENTATION
#ifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#ifdef __WINDOWS__
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#define GLFW_INCLUDE_NONE
#include "3rdparty/glad/include/glad/glad.h"
#include "3rdparty/glad/src/glad.c"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
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
#include "utils.h"
#include "audio_host.h"
#include "midi.h"
#include "sampler.h"
#include "text_editor.h"
#include "svf_gain.h"
#include "http_fetch.h"
#include "extvector.h"
#include "morton.h"
#include "json.h"
#include "logo.h"
#include "crash.h"
#include "mac_touch_input.h"

#ifndef __APPLE__
RawPen *mac_pen_get(void) { return NULL; } // returns pointer to pen if valid, NULL if no pen
int mac_touch_get(RawTouch *out, int max_count, float fbw, float fbh) { return 0; } // returns number of touches
void mac_touch_init(void *cocoa_window) {}                                          // pass NSWindow* from GLFW
#endif

#define RESW 1920
#define RESH 1080

static song_base_t dummy_state;
song_base_t *G = &dummy_state;

static double click_mx, click_my, click_time;
static int click_count;
// multitouch dragging:
static int prev_num_finger_dragging = 0;
static float prev_drag_cx = 0.f, prev_drag_cy = 0.f, prev_drag_dist = 0.f;
static int num_finger_dragging = 0;
static float drag_cx = 0.f, drag_cy = 0.f, drag_dist = 0.f;

static inline bool is_two_finger_dragging(void) { return num_finger_dragging == 2 && prev_num_finger_dragging == 2; }

void update_multitouch_dragging(void) {
    prev_num_finger_dragging = num_finger_dragging;
    prev_drag_cx = drag_cx;
    prev_drag_cy = drag_cy;
    prev_drag_dist = drag_dist;
    RawTouch touches[16];
    int num_touches = mac_touch_get(touches, 16, G->fbw, G->fbh);
    drag_cx = 0.f;
    drag_cy = 0.f;
    num_finger_dragging = 0;
    for (int i = 0; i < num_touches; i++) {
        // printf("%d %f %f :\n", touches[i].id, touches[i].x, touches[i].y);
        num_finger_dragging++;
        drag_cx += touches[i].x;
        drag_cy += touches[i].y;
    }
    if (num_finger_dragging)
        drag_cx /= num_finger_dragging, drag_cy /= num_finger_dragging;
    // printf("dragging: %d\n", num_finger_dragging);
    if (num_finger_dragging > 1) {
        float xx = 0.f, yy = 0.f;
        for (int i = 0; i < num_touches; i++) {
            xx += square(touches[i].x - drag_cx);
            yy += square(touches[i].y - drag_cy);
        }
        xx /= num_finger_dragging;
        yy /= num_finger_dragging;
        drag_dist = sqrtf(square(xx) + square(yy));
        // printf("drag_dist: %f\n", drag_dist);
    }
}

// Named constants for magic numbers
#define BLOOM_FADE_FACTOR (1.f / 16.f)
#define BLOOM_SPIKEYNESS 0.3f // 0 means 'no bloom spread' (infinitely spikey)
#define BLOOM_KERNEL_SIZE_DOWNSAMPLE 1.5f
#define BLOOM_KERNEL_SIZE_UPSAMPLE 3.f
#define CURSOR_SMOOTH_FACTOR 0.2f

// Status bar system is defined in text_editor.h

static void die(const char *msg, const char *param = "") {
    fprintf(stderr, "ERR: %s: %s\n", msg, param);
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
    float p0x, p0y;
    float p1x, p1y;
    float width, softness;
    int character;
    uint32_t col;
} line_t;

#define MAX_LINES (1 << 20)
uint32_t line_count = 0;
line_t lines[MAX_LINES];

void add_line(float p0x, float p0y, float p1x, float p1y, uint32_t col, float width, float softness, int character) {
    if (line_count >= MAX_LINES)
        return;
    lines[line_count] = (line_t){p0x, p0y, p1x, p1y, width, softness, character, col};
    line_count++;
}

static void uniform1i(GLuint shader, const char *uniform_name, int value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform1i(loc, value);
}
static void uniform1ui(GLuint shader, const char *uniform_name, uint32_t value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform1ui(loc, value);
}
static void uniform1fv(GLuint shader, const char *uniform_name, int count, const float *value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform1fv(loc, count, value);
}
static void uniform3fv(GLuint shader, const char *uniform_name, int count, const float *value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform3fv(loc, count, value);
}
static void uniform4fv(GLuint shader, const char *uniform_name, int count, const float *value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform4fv(loc, count, value);
}
static void uniform2i(GLuint shader, const char *uniform_name, int value1, int value2) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform2i(loc, value1, value2);
}
static void uniform1f(GLuint shader, const char *uniform_name, float value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform1f(loc, value);
}
static void uniform2f(GLuint shader, const char *uniform_name, float value1, float value2) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform2f(loc, value1, value2);
}
static void uniform3f(GLuint shader, const char *uniform_name, float value1, float value2, float value3) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform3f(loc, value1, value2, value3);
}
static void uniform4f(GLuint shader, const char *uniform_name, float value1, float value2, float value3, float value4) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform4f(loc, value1, value2, value3, value4);
}
static void uniformMatrix4fv(GLuint shader, const char *uniform_name, int count, GLboolean transpose, const float *value) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniformMatrix4fv(loc, count, transpose, value);
}

static void bind_texture_to_slot(GLuint shader, int slot, const char *uniform_name, GLuint texture, GLenum mag_filter) {
    int loc = glGetUniformLocation(shader, uniform_name);
    if (loc == -1)
        return;
    glUniform1i(loc, slot);
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

static void setup_framebuffers_and_textures(GLuint *texFPRT, GLuint *fbo, int num_bloom_mips, int num_fprts) {
    for (int i = 0; i < num_fprts + num_bloom_mips; ++i) {
        glGenFramebuffers(1, &fbo[i]);
        texFPRT[i] = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_BORDER);
        glBindTexture(GL_TEXTURE_2D, texFPRT[i]);
        int resw = (i < num_fprts) ? RESW : (RESW >> (i - num_fprts + 1));
        int resh = (i < num_fprts) ? RESH : (RESH >> (i - num_fprts + 1));
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
    uniform2f(shader, "duv", kernel_size / resw, kernel_size / resh);
    uniform1f(shader, "fade", fade_factor);
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

const char *kVS_stroke = SHADER(
    layout(location=0) in vec4 in_xy_radii;
    layout(location=1) in vec4 in_col;
    uniform vec2 fScreenPx;
    uniform vec3 center_and_zoom;
    out vec4 v_col;
    out vec4 v_uv_radii;
    void main() {
        vec2 xy = in_xy_radii.xy;
        // 0    1/5
        // 2/4    3
        int corner = gl_VertexID % 6;
        vec2 uv;
        uv.x = ((corner&1)!=0) ? 1.0 : -1.0;
        uv.y = (corner>=2 && corner<=4) ? 1.0 : -1.0;
        uv *= in_xy_radii.ww;
        xy += uv;
        v_uv_radii = vec4(uv, in_xy_radii.zw);
        v_col = in_col;
        xy = xy * center_and_zoom.z + center_and_zoom.xy;
        xy.y = fScreenPx.y - xy.y;
        gl_Position = vec4(xy * 2.0 / fScreenPx - 1.0, 0.0, 1.0);
    });
const char *kFS_stroke = SHADER(
    in vec4 v_col;
    in vec4 v_uv_radii;
    out vec4 o_color;
    void main() {
        float d = length(v_uv_radii.xy);
        vec4 col = vec4(v_col.xyz * v_col.w, v_col.w);
        float alpha = clamp(1.f-(d-v_uv_radii.z)/(v_uv_radii.w-v_uv_radii.z), 0.f, 1.f) * 0.5f;
        o_color = col * alpha;
    }
    );
const char *kVS_fat = SHADER(
    layout(location=0) in vec2 in_p1;
    layout(location=1) in vec2 in_p2;
    layout(location=2) in vec2 in_width_softness;
    layout(location=3) in int in_character;
    layout(location=4) in vec4  in_col; // from normalized ubyte4
    uniform ivec2 uFontPx;
    out vec2 v_uv;
    out vec2 v_txt;
    out vec4 v_col;
    out vec4 v_width_len_softness_character;
    uniform vec2 fScreenPx;
    void main() {
        vec2 p1 = in_p1, p2 = in_p2;
        float hw = abs(0.5 * in_width_softness.x);
        float hw_plus_1 = hw+1.f;
        vec2 dir = p2-p1;
        float len = length(dir);
        vec2 t = (len<1e-6) ? vec2(0.0f, hw_plus_1) : dir * -(hw_plus_1/len);
        vec2 n = vec2(-t.y, t.x);
        // 0    1/5
        // 2/4    3
        int corner = gl_VertexID % 6;
        vec2 p = p1;
        float vhw = hw_plus_1; 
        if (in_width_softness.x < 0.) { vhw = 0.; t=vec2(0.); } // square cap if in_width is negative
        if (in_character != 0) { vhw *= 0.5f; t*=0.5f; } // half width for characters
        vec2 uv = vec2(-hw,-vhw);
        if ((corner&1)==1) { t=-t; p = p2; uv.y = len+vhw; v_txt.x = 0.85f/16.f; } else v_txt.x = 0.15f/16.f;
        if (corner>=2 && corner<=4) { n=-n; uv.x=-uv.x; v_txt.y = 0.85f/6.f; } else v_txt.y = 0.15f/6.f;
        int ch = in_character-32;
        v_txt += vec2(1.f/16.f * (ch&15), 1.f/6.f * (ch/16));
        p=p+t+n;
        v_uv  = uv;
        v_col = in_col;
        float sdf_scale;
        if (in_width_softness.y >=0.f) {
            sdf_scale = 1.f/(1.f+in_width_softness.y);
        } else {
            sdf_scale = in_width_softness.y;
        }
        
        v_width_len_softness_character = vec4(hw, len, sdf_scale, in_character);
        p*=2.f/fScreenPx;
        p-=1.f;
        p.y=-p.y;
        gl_Position = vec4(p, 0.0, 1.0);
        });

const char *kFS_fat = SHADER(
    float saturate(float x) { return clamp(x, 0.0, 1.0); }
    uniform ivec2 uFontPx;
    uniform sampler2D uFont; 
    
    out vec4 o_color; 
    in vec2 v_uv; 
    in vec2 v_txt;
    in vec4 v_col;
    in vec4 v_width_len_softness_character;
    void main() {
        vec2 uv = v_uv;
        if (uv.y>0.) uv.y = max(0., uv.y - v_width_len_softness_character.y);
        float sdf;
        if (v_width_len_softness_character.w == 0) {
            sdf = v_width_len_softness_character.x-length(uv);
            if (v_width_len_softness_character.z > 0.) {
               sdf *= v_width_len_softness_character.z;
            } else {
                sdf = -(abs(sdf + v_width_len_softness_character.z) + v_width_len_softness_character.z);
            }
        } else {
            sdf=(texture(uFont, v_txt).r - 0.5f) * v_width_len_softness_character.x * 0.5f + 0.5f;
        }
        o_color=v_col * saturate(sdf);
    });

const char *kVS = SHADER(
out vec2 v_uv; 
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
uniform float t; // bpm synced time
uvec4 seed; 
uniform float cc[8];
uniform sampler2D uFP; 
uniform ivec2 uScreenPx;
uniform ivec2 uFontPx;
uniform sampler2D uFont; 
uniform usampler2D uText; 
uniform vec4 levels;
uniform vec4 levels_smooth;
uniform vec2 scroll; 
uniform vec4 cursor; 
uniform float ui_alpha;
uniform int status_bar_size;
uniform sampler2D uBloom;
uniform vec2 uARadjust;
uniform sampler2D uSky;
uniform sampler2D uTex;
uniform samplerBuffer uSpheres;
uniform mat4 c_cam2world;
uniform mat4 c_cam2world_old;
uniform vec4 c_lookat;
uniform float fov;
uniform float focal_distance;
uniform float aperture;
// uniform sampler2D uPaperDiff;
// uniform sampler2D uPaperDisp;
// uniform sampler2D uPaperNorm;
const float PI = 3.14159265358979323846f;
uvec4 pcg4d() {
    uvec4 v = seed * 1664525u + 1013904223u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    v ^= v >> 16u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    seed += v;
    return v;
}
uvec4 pcg4d_seed(inout uvec4 seed) {
    uvec4 v = seed * 1664525u + 1013904223u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    v ^= v >> 16u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    seed += v;
    return v;
}

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float square(float x) { return x * x; }
float lengthsq(vec2 v) { return dot(v, v); }
float lengthsq(vec3 v) { return dot(v, v); }
float lengthsq(vec4 v) { return dot(v, v); }
vec3 safe_normalize(vec3 p) {
	float l =lengthsq(p);
    if (l>1e-10) p*=inversesqrt(l);
    return p;
}

float ndot( in vec2 a, in vec2 b ) { return a.x*b.x - a.y*b.y; }
// awesome sdf functions following the naming and code of https://iquilezles.org/articles/distfunctions/
// thankyou inigo :)

float aa(float sdf) { return saturate(sdf*-1080.f+0.5f); }
float sdCircle( vec2 p, float r ) { return length(p) - r; }
float sdRect( in vec2 p, in vec2 b ) { vec2 d = abs(p)-b; return length(max(d,0.0)) + min(max(d.x,d.y),0.0); }
float sdRoundedRect( in vec2 p, in vec2 b, in vec4 r ) {
    r.xy = (p.x>0.0)?r.xy : r.zw;
    r.x  = (p.y>0.0)?r.x  : r.y;
    vec2 q = abs(p)-b+r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}
mat2 rot2(float a) { float c=cos(a), s=sin(a); return mat2(c,s,-s,c); }


float sdSphere( vec3 p, float s ) { return length(p)-s; }
float sdBox( vec3 p, vec3 b ) { vec3 q = abs(p) - b; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0); }
float sdRoundBox( vec3 p, vec3 b, float r ) { vec3 q = abs(p) - b + r; return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0) - r; }
float sdTorus( vec3 p, vec2 t ) { vec2 q = vec2(length(p.xz)-t.x,p.y); return length(q)-t.y; }
float sdCylinder( vec3 p, vec3 c ) { return length(p.xz-c.xy)-c.z; }
float sdPlane( vec3 p, vec3 n_norm, float h ) { return dot(p,n_norm) + h; }
float sdOctahedron( vec3 p, float s) { p = abs(p); return (p.x+p.y+p.z-s)*0.57735027; }
float sdEllipsoid( vec3 p, vec3 r ) { float k0 = length(p/r); float k1 = length(p/(r*r)); return k0*(k0-1.0)/k1; }
float sdCappedCylinder( vec3 p, float r, float h ) { vec2 d = abs(vec2(length(p.xz),p.y)) - vec2(r,h); return min(max(d.x,d.y),0.0) + length(max(d,0.0)); }
float sdCapsule( vec3 p, vec3 a, vec3 b, float r ) { vec3 pa = p - a, ba = b - a; float h = clamp( dot(pa,ba)/dot(ba,ba), 0.0, 1.0 ); return length( pa - ba*h ) - r; }
float opUnion( float d1, float d2 ) { return min(d1,d2); }
float opSubtraction( float d1, float d2 ) { return max(-d1,d2); }
float opIntersection( float d1, float d2 ) { return max(d1,d2); }
float opXor( float d1, float d2 ) { return max(min(d1,d2),-max(d1,d2)); }
float opSmoothUnion( float d1, float d2, float k ) { k *= 4.0; float h = max(k-abs(d1-d2),0.0); return min(d1, d2) - h*h*0.25/k; }
float opSmoothSubtraction( float d1, float d2, float k ) { return -opSmoothUnion(d1,-d2,k);}
float opSmoothIntersection( float d1, float d2, float k ) { return -opSmoothUnion(-d1,-d2,k); }

vec2 wave_read(float x, int y_base) {
    int ix = int(x);
    vec2 s0 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    ix++;
    vec2 s1 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    return mix(s0, s1, fract(x));
}
//vec2 scope(float x) { return (wave_read(x, 256 - 4) - 128.f) * (1.f/128.f); }
float fft(float x) { return (wave_read(x, 256 - 12).x) * (1.f/256.f); }

vec4 rnd4() { return vec4(pcg4d()) * (1.f / 4294967296.f); }
vec4 rnd4_seed(inout uvec4 seed) { return vec4(pcg4d_seed(seed)) * (1.f / 4294967296.f); }
vec2 rnd_disc_cauchy(vec2 z) {
    vec2 h = z * vec2(6.28318530718, 3.1 / 2.);
    h.y = tan(h.y);
    return h.y * vec2(sin(h.x), cos(h.x));
}
vec2 rnd_disc(vec2 z) {
    vec2 h = z * vec2(6.28318530718, 3.1 / 2.);
    h.y = sqrt(h.y);
    return h.y * vec2(sin(h.x), cos(h.x));
}
vec3 rnd_dir(vec2 zz) {
    float z = 1.f - zz.x * 2.f;
    float a = zz.y * 6.28318530718;
    float r = sqrt(1.f - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}
vec3 rnd_dir_cos(vec3 n, vec2 zz) {
    return normalize(n+rnd_dir(zz)*0.9999f);
}
// sphere of size ra centered at origin
vec2 sphere_intersect( in vec3 ro, in vec3 rd, float ra ) {
    float b = dot( ro, rd );
    vec3 qc = ro - b*rd;
    float h = ra*ra - dot( qc, qc );
    if( h<0.0 ) return vec2(-1.0); // no intersection
    h = sqrt( h );
    return vec2( -b-h, -b+h );
}

vec2 aabb_intersect(vec3 ro, vec3 inv_rd, vec3 boxmin, vec3 boxmax) {
    vec3 t0 = (boxmin - ro) * inv_rd;
    vec3 t1 = (boxmax - ro) * inv_rd;
    vec3 tmin_v = min(t0, t1);
    vec3 tmax_v = max(t0, t1);
    float tmin = max(max(tmin_v.x, tmin_v.y), tmin_v.z);
    float tmax = min(min(tmax_v.x, tmax_v.y), tmax_v.z);
    return vec2(tmin, tmax);
}

void eyeray(float t, out vec3 ro, out vec3 rd) {
    vec4 r4 = rnd4();
    mat4 mat = c_cam2world_old + (c_cam2world - c_cam2world_old) * t;
    vec2 uv = (v_uv - 0.5) * fov;
    uv.x *= 16./9.;
    uv += (r4.xy-0.5) * 1.f/1080.;
    // choose a point on the focal plane, assuming camera at origin
    vec3 rt = (mat[2].xyz + uv.x * mat[0].xyz + uv.y * mat[1].xyz) * focal_distance;
    if (aperture != 0.0) {
        // choose a point on the lens
        vec2 lensuv = rnd_disc(r4.zw)*aperture;
        ro = mat[0].xyz * lensuv.x + mat[1].xyz * lensuv.y;
    } else ro=vec3(0.);
    rd = rt-ro;
    ro+=mat[3].xyz; // add in camera pos
    rd=normalize(rd);
}

vec4 get_text_pixel(vec2 fpixel, ivec2 ufontpx, float grey, float contrast, float fatness) {
    ivec2 cell = ivec2(fpixel) / ufontpx; 
    uvec4 char_attrib = texelFetch(uText, cell, 0);
    int ascii = int(char_attrib.r);
    vec4 fg = vec4(1.0), bg = vec4(1.0);
    fg.x = float(char_attrib.b & 15u) * (1.f / 15.f);
    fg.y = float(char_attrib.g >> 4u) * (1.f / 15.f);
    fg.z = float(char_attrib.g & 15u) * (1.f / 15.f);
    bg.x = float(char_attrib.a >> 4) * (1.f / 15.f);
    bg.y = float(char_attrib.a & 15u) * (1.f / 15.f);
    bg.z = float(char_attrib.b >> 4) * (1.f / 15.f);
    vec2 cellpix = vec2(fpixel - cell * ufontpx + 0.5f) / vec2(ufontpx);
    vec2 fontpix=vec2(cellpix.x*0.75f+0.125f + (ascii & 15), cellpix.y*0.75f+0.125f + (ascii >> 4)-2) * vec2(1./16.,1./6.);
    float sdf_level = 0.f;
    float aa = ufontpx.x * contrast;
    if (ascii<128) {
        sdf_level = texture(uFont, fontpix).r - fatness;
    } else if (ascii<128+16) {
        // vertical bar:
        sdf_level = 4.f * (cellpix.y -1.f + float(ascii-128+1)*(1.f/16.f)); // bar graph
    } else {
        // horizontal slider tick:
        sdf_level = 0.2f - 4.f * abs(cellpix.y-0.5f);
        sdf_level=max(sdf_level, 0.5f - 2.f * abs(cellpix.x+0.25f-float(ascii-(128+16))*1.f/8.f));
    }
    float fontlvl = (ascii >= 32) ? sqrt(saturate(sdf_level * aa + 0.5)) : 0.0;
    if (bg.x <= 1.f/15.f && bg.y <= 1.f/15.f && bg.z <= 1.f/15.f) {
        bg.w = bg.x * 4.f;
        bg.xyz = vec3(0.f);
    }
    if (ascii != 0) {
        bg.w=max(grey, bg.w);
    }
    return mix(bg, fg, fontlvl);
}
// user code follows
);
    
const char *kFS_user_suffix = SHADER_NO_VERSION(
    void main() {
        vec2 fragCoord = gl_FragCoord.xy;
        seed = uvec4(uint(fragCoord.x), uint(fragCoord.y), uint(iFrame) , uint(23952683));
        vec2 uv = (v_uv - 0.5);
        uv.x *= 16./9.;
        o_color = pixel(uv);
    });

const char *kFS_bloom = SHADER(
    out vec4 o_color; 
    in vec2 v_uv; 
    uniform sampler2D uFP; 
    uniform vec2 duv;
    uniform float fade;
    void main() {
        
        vec3 c0 = texture(uFP, v_uv + vec2(-duv.x, -duv.y)).xyz;
        c0     += texture(uFP, v_uv + vec2(     0, -duv.y)).xyz * 2.;
        c0     += texture(uFP, v_uv + vec2( duv.x, -duv.y)).xyz;
        c0     += texture(uFP, v_uv + vec2(-duv.x,      0)).xyz * 2.;
        c0     += texture(uFP, v_uv + vec2(     0,      0)).xyz * 4.;
        c0     += texture(uFP, v_uv + vec2( duv.x,      0)).xyz * 2.;
        c0     += texture(uFP, v_uv + vec2(-duv.x,  duv.y)).xyz;
        c0     += texture(uFP, v_uv + vec2(     0,  duv.y)).xyz * 2.;
        c0     += texture(uFP, v_uv + vec2( duv.x,  duv.y)).xyz;        
        o_color = vec4(c0 * fade, 16.f * fade);
    }
); 

const char *kFS_taa = SHADER(
    uniform ivec2 uScreenPx; 
    in vec2 v_uv;
    uniform sampler2D uFP; 
    uniform sampler2D uFP_prev;
    out vec4 o_color; 
    uniform float fov;
    uniform mat4 c_cam2world;
    uniform mat4 c_cam2world_old;
    
    void main() {
        vec2 fov2 = vec2(fov * 16./9., fov);
        ivec2 pixel = ivec2(gl_FragCoord.xy);
        vec4 newcol_depth = texelFetch(uFP, pixel, 0);
        float depth = 1.f / newcol_depth.w;
        vec3 mincol = newcol_depth.xyz;
        vec3 maxcol = newcol_depth.xyz;
        //vec3 avgcol = newcol_depth.xyz;
        vec3 c;
        c=texelFetch(uFP, pixel + ivec2(-1, 1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2( 0, 1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2( 1, 1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2(-1, 0), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2( 1, 0), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2(-1,-1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2( 0,-1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        c=texelFetch(uFP, pixel + ivec2( 1,-1), 0).xyz; mincol = min(mincol, c); maxcol = max(maxcol, c); //avgcol += c;
        vec3 o = newcol_depth.xyz;
        // o=o*o;
        // mincol=mincol*mincol;
        // maxcol=maxcol*maxcol;
        
        //avgcol *= 1.f/9.f;
        // reproject old frame
        vec2 uv = (v_uv - 0.5) * fov2;
        // todo - why do we need these normalizes, the matrix should be orthonormal!
        vec3 rd_new = normalize(normalize(c_cam2world[2].xyz) + uv.x * normalize(c_cam2world[0].xyz) + uv.y * normalize(c_cam2world[1].xyz));
        vec3 hit = c_cam2world[3].xyz + rd_new * depth;
        hit -= c_cam2world_old[3].xyz;
        float ww = 1. / dot(hit, (c_cam2world_old[2].xyz));
        float uu = ww * dot(hit, (c_cam2world_old[0].xyz)) / fov2.x + 0.5;
        float vv = ww * dot(hit, (c_cam2world_old[1].xyz)) / fov2.y + 0.5;    
        if (uu<=0. || vv<=0. || uu>=1. || vv>=1. || ww<=0. || isnan(ww)) 
        {
            // no history.
        } else {
            vec3 history = texture(uFP_prev, vec2(uu,vv)).xyz;
            float believe_history = 0.95f;
            history = max(history, mincol);
            history = min(history, maxcol);
            o = mix(o, history, believe_history);
        }
        //o.xyz = o.xyz / (1.0-min(0.99f,dot(o.xyz,vec3(0.01)))); // re-expand
        o_color = vec4(o, 1.f);
    }
);

const char *kFS_secmon = SHADER(
    in vec2 v_uv;
    uniform sampler2D uFP;
    uniform sampler2D uBloom;
    uniform vec2 uARadjust;
    out vec4 o_color;
    vec3 aces(vec3 x) {
        const float a=2.51;
        const float b=0.03;
        const float c=2.43;
        const float d=0.59;
        const float e=0.14;
        return clamp((x*(a*x+b)) / (x*(c*x+d)+e), 0.0, 1.0);
    }
    void main() {
        vec2 user_uv = (v_uv-0.5) * uARadjust + 0.5;
        vec3 rendercol= texture(uFP, user_uv).rgb;
        vec3 bloomcol= texture(uBloom, user_uv).rgb;
        rendercol += bloomcol;// * 0.3;
        rendercol = max(vec3(0.), rendercol);
        rendercol.rgb = sqrt(aces(rendercol.rgb));
        o_color = vec4(rendercol.xyz, 1.0);
    }
);


const char *kFS_ui_suffix = SHADER_NO_VERSION(
    uniform float fade_render;
    vec3 aces(vec3 x) {
        const float a=2.51;
        const float b=0.03;
        const float c=2.43;
        const float d=0.59;
        const float e=0.14;
        return clamp((x*(a*x+b)) / (x*(c*x+d)+e), 0.0, 1.0);
    }
    // vec2 wave_read(float x, int y_base) {
    //     int ix = int(x);
    //     vec2 s0 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    //     ix++;
    //     vec2 s1 = vec2(texelFetch(uText, ivec2(ix & 511, y_base + (ix >> 9)), 0).xy);
    //     return mix(s0, s1, fract(x));
    // }

    // vec2 scope(float x) { return (wave_read(x, 256 - 4) - 128.f) * 0.25; }
    
    void main() {
        // adjust for aspect ratio
        vec2 user_uv = (v_uv-0.5) * uARadjust + 0.5;
        vec3 rendercol= texture(uFP, user_uv).rgb;
        vec3 bloomcol= texture(uBloom, user_uv).rgb;
        rendercol += bloomcol ;//* 0.3;
        rendercol = max(vec3(0.), rendercol);
        
        vec2 pix = (v_uv) * vec2(uScreenPx.x, 2048.f);
        float fftx = uScreenPx.x - pix.x;
        if (pix.x < 64.f || fftx<128.f) {
            bool is_fft = false;
            float ampscale = 0.25f;
            int base_y = 256 - 4;
            float curx = pix.y;
            float nextx = pix.y + 1.;
            if (fftx < 128.f) {
                ampscale = 0.5f;
                is_fft = true;
                float dither = fract(pix.x*23.5325f) / 2048.f;
                curx = (48.f / 48000.f * 4096.f) * pow(1000.f, v_uv.y + dither); // 24hz to nyquist
                curx*=2.f;

                base_y = 256 - 12;
                pix.x = fftx;
                nextx = curx * 1.003;
            } else {
                curx -= 96.f;
                nextx -= 96.f;
            }
            vec2 prevy = wave_read(curx*0.5f, base_y) * ampscale;
            vec2 nexty = wave_read(nextx*0.5f, base_y) * ampscale;
            vec2 miny = min(prevy, nexty);
            vec2 maxy = max(prevy, nexty);
            vec2 beam = smoothstep(maxy + 1.f, miny - 0.5f, pix.xx);
            vec3 beamcol;
            if (is_fft) {
                //beam *= 0.25;
                float lum = maxy.x / 128.f;
                //lum*=lum;
                beamcol = 0.1f + vec3(lum, lum*lum, lum*lum*lum);
                beamcol *= beam.y;
            } else {
                beam *= smoothstep((64.f-maxy) -0.5f, (64.f-miny) +1.f, pix.xx);
                beamcol = vec3(0.2, 0.07, 0.02) * beam.x + vec3(0.02, 0.07, 0.2) * beam.y;
            }
            if (gl_FragCoord.x<64. && gl_FragCoord.y<64.) {
                int idx = int(gl_FragCoord.x) + int(gl_FragCoord.y) * 64;
                uvec4 xyscope = texelFetch(uText, ivec2(idx & 511, (256-20) + (idx >> 9)), 0);
                beamcol = vec3(float(xyscope.x + xyscope.y * 256u) * vec3(0.00004,0.00008,0.00006));
            }
            rendercol += max(vec3(0.),beamcol);
        } 
        if (gl_FragCoord.x >= 64. && gl_FragCoord.y < 64.) {
            const float yscale = 64. / 256.f;
            float x = gl_FragCoord.x * 0.5f;
            vec2 nextw = wave_read(x+0.5f, 256-2) * yscale;
            vec2 prevw = wave_read(x, 256-2) * yscale;
            vec2 minw = min(nextw,prevw);
            vec2 maxw = max(nextw,prevw);
            vec2 midw = maxw+minw;
            vec2 widw = maxw-minw;
            vec2 beam = smoothstep(widw+2., widw+1., abs(midw-vec2(pix.y))) * 4.;
            vec3 beamcol = vec3(0.2, 0.07, 0.02) * beam.x + vec3(0.02, 0.07, 0.2) * beam.y;
            rendercol += max(vec3(0.),beamcol);
        }    
        
        rendercol.rgb = clamp(sqrt(aces(rendercol.rgb)) * fade_render, 0.f, 1.f);
        float grey = dot(rendercol, vec3(0.2126 * 0.5, 0.7152 * 0.5, 0.0722 * 0.5));

        vec2 fpixel = vec2(v_uv.x * uScreenPx.x, (1.0 - v_uv.y) * uScreenPx.y);
        // status line doesnt scroll
        float status_bar_y = uScreenPx.y - uFontPx.y * status_bar_size;
        fpixel.x += scroll.x;
        if (fpixel.y < status_bar_y) {
            fpixel.y += scroll.y;
        } else {
            int TMH = 256;
            fpixel.y = (fpixel.y - status_bar_y) + uFontPx.y * (TMH-21);
        }
        vec4 fontcol = get_text_pixel(fpixel, uFontPx, grey, 0.5f, 0.5f);
     
        vec2 fcursor = vec2(cursor.x, cursor.y) - fpixel;
        vec2 fcursor_prev = vec2(cursor.z, cursor.w) - fpixel;
        vec2 cursor_delta = fcursor_prev - fcursor;
        float cursor_t = clamp(-fcursor.x / (cursor_delta.x + 1e-6 * sign(cursor_delta.x)), 0., 1.);
        fcursor += cursor_delta * cursor_t;
        if (fcursor.x >= -2.f && fcursor.x <= 2.f && fcursor.y >= -float(uFontPx.y) && fcursor.y <= 0.f) {
            float cursor_alpha = 1.f - cursor_t * cursor_t;
            float cursor_col = 1.f; // (grey > 0.5) ? 0. : 1.;    
            fontcol = mix(fontcol, vec4(vec3(cursor_col), 1.), cursor_alpha);
        }
        fontcol*=ui_alpha;
        o_color = vec4(rendercol.xyz * (1.-fontcol.w) + fontcol.xyz, 1.0);
    });
// clang-format on

extern EditorState tabs[TAB_LAST];
EditorState tabs[TAB_LAST] = {
    {.fname = NULL, .editor_type = TAB_SHADER},
    {.fname = NULL, .editor_type = TAB_AUDIO},
    {.editor_type = TAB_CANVAS},
    {.editor_type = TAB_SAMPLES},

};
EditorState *curE = tabs;
size_t textBytes = (size_t)(TMW * TMH * 4);
static float retina = 1.0f;

GLuint ui_pass = 0, user_pass = 0, bloom_pass = 0, taa_pass = 0, secmon_pass = 0;
GLuint vs = 0, fs_ui = 0, fs_bloom = 0, fs_taa = 0, fs_secmon = 0;
GLuint stroke_prog = 0;
GLuint pbos[3];
int pbo_index = 0;
GLuint texFont = 0, texText = 0;
int skytex = 0, textex = 0;

void set_tab(EditorState *newE) {
    G->ui_alpha_target = (G->ui_alpha_target > 0.5 && curE == newE) ? 0.f : 1.f;
    curE = newE;
}

int update_pattern_color_bitmask(pattern_t *patterns, pattern_t *root, pattern_t *p, int depth) {
    int mask = 0;
    for (int j = 0; j < stbds_arrlen(p->bfs_nodes); j++) {
        if (p->bfs_nodes[j].type == N_COLOR) {
            mask |= 1 << (int)p->bfs_min_max_value[j].mx;
        }
        if (p->bfs_nodes[j].type == N_NEAR) {
            mask |= 1 << 24;
            int pidx = (int)p->bfs_min_max_value[j].mx;
            if (pidx >= 0 && pidx < stbds_shlen(patterns)) {
                pattern_t *target_pat = &patterns[pidx];
                target_pat->colbitmask |= 1 << 28; // target of a near... to make the node appear in the canvas.
            }
        }
        if (p->bfs_nodes[j].type == N_BLENDNEAR) {
            mask |= 1 << 25;
            int first_child = p->bfs_nodes[j].first_child;
            for (int ch = 0; ch < p->bfs_nodes[j].num_children; ch++) {
                int pidx = (int)p->bfs_min_max_value[first_child + ch].mx;
                if (pidx >= 0 && pidx < stbds_shlen(patterns)) {
                    pattern_t *target_pat = &patterns[pidx];
                    target_pat->colbitmask |= 1 << 29; // target of a blendnear... to make the node appear in the canvas.
                }
            }
        }

        /* TODO: add upper bits and _color words to allow for nested colors via call.
        if (p->bfs_nodes[j].type == N_CALL && depth < 4) {
            int patidx = (int)p->bfs_min_max_value[j].mx;
            if (patidx >= 0 && patidx < stbds_shlen(patterns)) {
                pattern_t *pat = &patterns[patidx];
                mask |= update_pattern_color_bitmask(patterns, root, pat, depth + 1);
            }
        }*/
    }
    return mask;
}

void update_pattern_color_bitmasks(pattern_t *patterns) {
    for (int i = 0; i < stbds_shlen(patterns); i++) {
        pattern_t *p = &patterns[i];
        p->colbitmask = update_pattern_color_bitmask(patterns, p, p, 0);
    }
}

void update_pattern_uniforms(pattern_t *patterns, pattern_t *prev_patterns) {
    for (int i = 0; i < stbds_shlen(patterns); i++) {
        pattern_t *p = &patterns[i];
        pattern_t *prev_p = stbds_shgetp_null(prev_patterns, p->key);
        if (prev_p) {
            p->shader_param = prev_p->shader_param;
            p->x = prev_p->x;
            p->y = prev_p->y;
        }
        p->uniform_idx = -1;
        if (user_pass && p->key && p->key[0] == '/') {
            p->uniform_idx = glGetUniformLocation(user_pass, p->key + 1);
        }
    }
}

void parse_named_patterns_in_source(void) {
    extern pattern_t *new_pattern_map_during_parse;
    new_pattern_map_during_parse = NULL; // stbds_hm
    // merge all patterns across the first 2 tabs....
    for (int tabi = 0; tabi < 2; ++tabi) {
        EditorState *E = &tabs[tabi];
        stbds_hmfree(E->error_msgs);
        E->error_msgs = NULL;
        const char *s = E->str, *real_e = E->str + stbds_arrlen(E->str);
        init_remapping(E);
        E->error_msgs = parse_named_patterns_in_source(E->str, E->str + stbds_arrlen(E->str), E->error_msgs);
    }
    update_pattern_color_bitmasks(new_pattern_map_during_parse);
    update_pattern_uniforms(new_pattern_map_during_parse, G->patterns_map);
    void update_all_pattern_over_colors(pattern_t * patterns, EditorState * E);
    update_all_pattern_over_colors(new_pattern_map_during_parse, &tabs[TAB_CANVAS]);
    // TODO - let the old pattern table leak because concurrency etc
    G->patterns_map = new_pattern_map_during_parse;
    new_pattern_map_during_parse = NULL;
}

static void dump_settings(void) {
    FILE *f = fopen("settings.json", "w");
    if (!f) {
        fprintf(stderr, "Failed to open settings.json for writing\n");
        return;
    }
    json_printer_t jp = {f};
    json_start_object(&jp, NULL);
    json_start_object(&jp, "camera");
    json_print(&jp, "c_pos", G->camera.c_pos);
    json_print(&jp, "c_lookat", G->camera.c_lookat);
    json_print(&jp, "fov", G->camera.fov);
    json_print(&jp, "focal_distance", G->camera.focal_distance);
    json_print(&jp, "aperture", G->camera.aperture);
    json_end_object(&jp);
    json_start_object(&jp, "editor");
    json_print(&jp, "ui_alpha_target", G->ui_alpha_target);
    json_print(&jp, "cur_tab", (int)(curE - tabs));
    json_start_array(&jp, "tabs");
    for (int tab = 0; tab < TAB_LAST; tab++) {
        EditorState *t = &tabs[tab];
        json_start_object(&jp, NULL);
        json_print(&jp, "fname", t->fname);
        json_print(&jp, "font_height", t->font_height);
        json_print(&jp, "scroll_x", t->scroll_x);
        json_print(&jp, "scroll_y", t->scroll_y);
        json_print(&jp, "cursor_idx", t->cursor_idx);
        json_print(&jp, "select_idx", t->select_idx);
        json_end_object(&jp);
    }
    json_end_file(&jp);
}

static void load_settings(int argc, char **argv, int *primon_idx, int *secmon_idx, bool *prefetch) {
    int cur_tab = 0;
    const char *settings_fname = "settings.json";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                printf(COLOR_YELLOW "Usage: " COLOR_CYAN "ginkgo" COLOR_RESET " [options] [filename]\n");
                printf("Options:\n");
                printf(COLOR_YELLOW "  --prefetch, -p " COLOR_RESET " - prefetch all sample assets\n");
                printf(COLOR_YELLOW "  --fullscreen, -f, -e index " COLOR_RESET
                                    " - run editor fullscreen on the specified monitor\n");
                printf(COLOR_YELLOW "  --settings, -s file.json " COLOR_RESET " - load settings from file\n");
                printf(COLOR_YELLOW "  --secmon, -m, -v index " COLOR_RESET " - use the specified monitor for visuals\n");
                printf(COLOR_YELLOW "  --help, -h " COLOR_RESET " - show this help message\n");
                printf("\nif filename is .glsl or .cpp it will be loaded as the first or second tab respectively\n");
                printf("if filename is .json it will be loaded as the settings file\n");
                _exit(0);
            }
            if (strcmp(argv[i], "--prefetch") == 0 || strcmp(argv[i], "-p") == 0) {
                *prefetch = true;
            }
            if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "-e") == 0) {
                if (i + 1 < argc)
                    *primon_idx = atoi(argv[i + 1]);
                else
                    *primon_idx = 0;
            }
            if (strcmp(argv[i], "--settings") == 0 || strcmp(argv[i], "-s") == 0) {
                if (i + 1 < argc)
                    settings_fname = argv[i + 1];
            }
            if (strcmp(argv[i], "--secmon") == 0 || strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "-v") == 0) {
                if (i + 1 < argc)
                    *secmon_idx = atoi(argv[i + 1]);
                else
                    *secmon_idx = 0;
            }
        } else if (strstr(argv[i], ".glsl"))
            tabs[TAB_SHADER].fname = stbstring_from_span(argv[i], NULL, 0);
        else if (strstr(argv[i], ".cpp"))
            tabs[TAB_AUDIO].fname = stbstring_from_span(argv[i], NULL, 0);
        else if (strstr(argv[i], ".json"))
            settings_fname = argv[i];
        else if (strstr(argv[i], ".canvas"))
            tabs[TAB_CANVAS].fname = stbstring_from_span(argv[i], NULL, 0);
    }
    sj_Reader r = read_json_file(settings_fname);
    for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
        if (iter_key_is(&outer, "camera")) {
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                if (iter_key_is(&inner, "c_pos")) {
                    G->camera.c_cam2world[3] = iter_val_as_float4(&inner, G->camera.c_cam2world[3]);
                } else if (iter_key_is(&inner, "c_lookat")) {
                    G->camera.c_lookat = iter_val_as_float4(&inner, G->camera.c_lookat);
                } else if (iter_key_is(&inner, "fov")) {
                    G->camera.fov = iter_val_as_float(&inner, G->camera.fov);
                } else if (iter_key_is(&inner, "focal_distance")) {
                    G->camera.focal_distance = iter_val_as_float(&inner, G->camera.focal_distance);
                } else if (iter_key_is(&inner, "aperture")) {
                    G->camera.aperture = iter_val_as_float(&inner, G->camera.aperture);
                }
            }
        } else if (iter_key_is(&outer, "editor")) {
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                if (iter_key_is(&inner, "ui_alpha_target")) {
                    G->ui_alpha_target = iter_val_as_float(&inner, G->ui_alpha_target);
                } else if (iter_key_is(&inner, "cur_tab")) {
                    cur_tab = iter_val_as_int(&inner, cur_tab);
                } else if (iter_key_is(&inner, "tabs")) {
                    int tabidx = 0;
                    for (sj_iter_t inner2 = iter_start(inner.r, &inner.val); tabidx < TAB_LAST && iter_next(&inner2); tabidx++) {
                        EditorState *t = &tabs[tabidx];
                        bool they_set_fname = t->fname != NULL;
                        for (sj_iter_t inner3 = iter_start(inner2.r, &inner2.val); iter_next(&inner3);) {
                            if (iter_key_is(&inner3, "font_height")) {
                                t->font_height = iter_val_as_int(&inner3, t->font_height);
                            }
                            if (they_set_fname)
                                continue; // they specified a new name on the commandline, ignore what we stored.
                            if (iter_key_is(&inner3, "fname")) {
                                t->fname = iter_val_as_stbstring(&inner3, t->fname);
                            } else if (iter_key_is(&inner3, "scroll_x") && t->scroll_x == 0.f) {
                                t->scroll_x = iter_val_as_float(&inner3, t->scroll_x);
                            } else if (iter_key_is(&inner3, "scroll_y") && t->scroll_y == 0.f) {
                                t->scroll_y = iter_val_as_float(&inner3, t->scroll_y);
                            } else if (iter_key_is(&inner3, "cursor_idx") && t->cursor_idx == 0) {
                                t->cursor_idx = iter_val_as_int(&inner3, t->cursor_idx);
                            } else if (iter_key_is(&inner3, "select_idx") && t->select_idx == 0) {
                                t->select_idx = iter_val_as_int(&inner3, t->select_idx);
                            }
                        } // tab settings
                        t->scroll_target_x = t->scroll_x;
                        t->scroll_target_y = t->scroll_y;
                        t->font_height = clamp(t->font_height, 8, 256);
                        t->font_width = t->font_height / 2;
                    }
                }
            }
        }
    }
    if (tabs[TAB_SHADER].fname == NULL)
        tabs[TAB_SHADER].fname = stbstring_from_span("livesrc/blank.glsl", NULL, 0);
    if (tabs[TAB_AUDIO].fname == NULL)
        tabs[TAB_AUDIO].fname = stbstring_from_span("livesrc/blank.cpp", NULL, 0);
    if (tabs[TAB_CANVAS].fname == NULL)
        tabs[TAB_CANVAS].fname = stbstring_from_span("livesrc/blank.canvas", NULL, 0);
    if (cur_tab < 0 || cur_tab >= TAB_LAST)
        cur_tab = 0;
    curE = &tabs[cur_tab];
    free_json(&r);
    G->ui_alpha = G->ui_alpha_target;
}

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

static bool try_to_compile_audio(char **errorlog) {
    if (errorlog) {
        stbds_arrfree(*errorlog);
        *errorlog = NULL;
    }
    char cmd[1024];
    int version = g_version + 1;
#ifdef __WINDOWS__
    mkdir("build");
#else
    mkdir("build", 0755);
#endif
#if USING_ASAN
#define SANITIZE_OPTIONS "-fsanitize=address"
#else
#define SANITIZE_OPTIONS ""
#endif
    FILE *f = fopen("build/stub.cpp", "w");
    if (!f)
        return false;
    fprintf(f,
            "#include \"ginkgo.h\"\n"
            "%.*s\n"
            "#include \"ginkgo_post.h\"\n",
            (int)stbds_arrlen(tabs[TAB_AUDIO].str), tabs[TAB_AUDIO].str);
    // add c portions of shader too!
    const char *code_start = tabs[TAB_SHADER].str;
    const char *s = code_start;
    const char *real_e = code_start + stbds_arrlen(code_start);
    while (s < real_e) {
        s = spanstr(s, real_e, "#ifdef C");
        if (!s)
            break;
        s += 8;
        if (s >= real_e || !isspace(*s))
            break;
        const char *e = spanstr(s, real_e, "#endif");
        if (!e)
            break;
        fprintf(f, "\n%.*s\n", (int)(e - s), s);
        s = e + 6;
    }
    fclose(f);
    snprintf(cmd, sizeof(cmd),
             "clang++ " CLANG_OPTIONS " " SANITIZE_OPTIONS " build/stub.cpp"
             " -o build/dsp.%d." BUILD_LIB_EXT " 2>&1",
             version);
    printf("[%s]\n", cmd);
    int64_t t0 = get_time_us();
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "popen %s failed: %s\n", cmd, strerror(errno));
        return false;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) {
        fprintf(stderr, "%s", buf);
        int n = strlen(buf);
        char *errbuf = stbds_arraddnptr(*errorlog, n);
        memcpy(errbuf, buf, n);
    }
    stbds_arrput(*errorlog, 0);
    int rc = pclose(fp);
    int64_t t1 = get_time_us();
    if (rc != 0)
        return false;
// this block unloads the old dll after forcing a null dsp pointer. causes a long click
// but stops asan from crashing
#if USING_ASAN
    atomic_store_explicit(&g_dsp_req, 0, memory_order_release);
    while (atomic_load_explicit(&g_dsp_used, memory_order_acquire) != 0) {
        usleep(1000);
    }
    dlclose(g_handle);
    g_handle = NULL;
#endif
    snprintf(cmd, sizeof(cmd), "build/dsp.%d." BUILD_LIB_EXT, version);
    void *h = dlopen(cmd, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen %s failed: %s\n", cmd, dlerror());
        return false;
    }
    g_frame_update_func = (frame_update_func_t)dlsym(h, "frame_update_func");
    dsp_fn_t fn = (dsp_fn_t)dlsym(h, "dsp");
    if (!fn) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(h);
        return false;
    }
    //////////////////////////////////
    // SUCCESS!
    atomic_store_explicit(&g_dsp_req, fn, memory_order_release);
    while (atomic_load_explicit(&g_dsp_used, memory_order_acquire) != fn) {
        usleep(1000);
    }
    if (g_handle)
        dlclose(g_handle);
    g_handle = h;
    g_version = version;
    fprintf(stderr, "compile %s succeeded in %.3fms\n", cmd, (t1 - t0) / 1000.0);
    snprintf(cmd, sizeof(cmd), "build/dsp.%d." BUILD_LIB_EXT, version - 1);
    unlink(cmd);
    return true;
}

GLuint try_to_compile_shader(EditorState *E) {
    if (E->editor_type != TAB_SHADER) {
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
        update_pattern_uniforms(G->patterns_map, NULL);
    }
    if (!user_pass)
        die("Failed to compile shader");
    glDeleteShader(fs_user);
    free(fs_user_str);
    GLenum e = glGetError(); // clear any errors
    return new_user_pass;
}
GLFWwindow *winFS; // fullscreen window for visuals
GLuint vaoFS = 0;

GLFWwindow *gl_init(int primon_idx, int secmon_idx) {

    int count = 0;
    GLFWmonitor **mons = glfwGetMonitors(&count);
    GLFWmonitor *actual_primon = glfwGetPrimaryMonitor();
    GLFWmonitor *primon = NULL;
    GLFWmonitor *secmon = NULL;
    if (primon_idx >= 0 && primon_idx < count)
        primon = mons[primon_idx];
    /*if (secmon_idx < 0 && count > 1) {
        // if more than 1 monitor, default to visuals on the secondary monitor
        GLFWmonitor *monitor_to_avoid = primon ? primon : actual_primon;
        for (int i = 0; i < count; i++)
            if (mons[i] != monitor_to_avoid) {
                secmon_idx = i;
                break;
            }
    }*/
    if (secmon_idx == primon_idx)
        secmon_idx = -1;
    if (secmon_idx >= 0 && secmon_idx < count)
        secmon = mons[secmon_idx];

    const GLFWvidmode *vm = primon ? glfwGetVideoMode(primon) : NULL;
    int ww = primon ? vm->width : 1920 / 2;
    int wh = primon ? vm->height : 1200 / 2;
    if (vm && (ww > vm->width || wh > vm->height)) {
        ww /= 2;
        wh /= 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, !primon);
#endif
    glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_FALSE);
    if (primon) {
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    }

    char winname[1024];
    snprintf(winname, sizeof(winname), "ginkgo | %s | %s", tabs[TAB_SHADER].fname, tabs[TAB_AUDIO].fname);
    GLFWwindow *win = glfwCreateWindow(ww, wh, winname, primon, NULL);
    if (!win)
        die("glfwCreateWindow failed");
    glfwGetWindowContentScale(win, &retina, NULL);
    // printf("retina: %f\n", retina);
    glfwMakeContextCurrent(win);
    if (!gladLoadGL())
        die("Failed to load OpenGL");

    glfwSwapInterval(secmon ? 0 : 1); // let the secondary monitor determine the swap interval
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    if (secmon) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, false);
#endif
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_FALSE);
        const GLFWvidmode *vm = glfwGetVideoMode(secmon);
        int ww = vm->width;                                              // 1920
        int wh = vm->height;                                             // 1080
        winFS = glfwCreateWindow(ww, wh, "Ginkgo Visuals", secmon, win); // 'share' = win
        glfwMakeContextCurrent(winFS);
        glfwSwapInterval(1);
        glGenVertexArrays(1, &vaoFS);
        // glfwSetInputMode(winFS, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        unsigned char px[4] = {0, 0, 0, 0};
        GLFWimage img = {.width = 1, .height = 1, .pixels = px};
        GLFWcursor *invis = glfwCreateCursor(&img, 0, 0);
        glfwSetCursor(winFS, invis);
        glfwMakeContextCurrent(win);
    }

    return win;
}

static uint32_t last_mods = 0;

static void key_callback(GLFWwindow *win, int key, int scancode, int action, int mods) {
// printf("key: %d\n", key);
#ifdef __WINDOWS__
    if (mods & GLFW_MOD_CONTROL) {
        mods &= ~GLFW_MOD_CONTROL;
        mods |= GLFW_MOD_SUPER;
    }
#endif
    last_mods = mods;
    EditorState *E = curE;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    if (mods == 0) {
        if (key >= GLFW_KEY_F1 && key < GLFW_KEY_F1 + TAB_LAST) {
            set_tab(&tabs[key - GLFW_KEY_F1]);
        }
        if (key == GLFW_KEY_F12) {
            // tap tempo
            static double last_time = 0;
            static double first_tap = 0;
            static int tap_count = 0;
            static uint64_t first_tap_t_q32;
            double now = glfwGetTime();
            if (now - last_time > 1.f) {
                // more than one second passed..
                tap_count = 1;
                first_tap = now;
                first_tap_t_q32 = (G->t_q32 + (1 << 28)) & ~((1 << 30) - 1);
                G->t_q32 = first_tap_t_q32;
            } else {
                if (G->playing)
                    G->t_q32 = first_tap_t_q32 + (tap_count * (1ull << 30));
                float bpm = (60.f / (now - first_tap)) * tap_count;
                if (tap_count > 1) {
                    fprintf(stdout, "tapped bpm: %f\n", bpm);
                    pattern_t *bpm_pattern = get_pattern("/bpm");
                    if (bpm_pattern && bpm_pattern->bfs_start_end) {
                        EditorState *audioE = &tabs[TAB_AUDIO];
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%.1f", bpm);
                        int si = bpm_pattern->bfs_start_end[0].start;
                        int ei = bpm_pattern->bfs_start_end[0].end;
                        push_edit_op(audioE, si, ei, buf, 0);
                        const char *pattern_start = audioE->str + si;
                        const char *pattern_end = pattern_start + strlen(buf);
                        pattern_maker_t p = {.s = pattern_start, .n = (int)(pattern_end - pattern_start)};
                        pattern_t pat = parse_pattern(&p, pattern_start - audioE->str);
                        if (p.err <= 0) {
                            pat.key = bpm_pattern->key;
                            *bpm_pattern = pat;
                        } else {
                            pat.unalloc();
                        }
                    }
                }
                tap_count++;
            }
            last_time = now;
        }
    }
    if ((key == GLFW_KEY_F4 || key == GLFW_KEY_Q) && (mods == GLFW_MOD_CONTROL || mods == GLFW_MOD_SUPER || mods == GLFW_MOD_ALT)) {
        glfwSetWindowShouldClose(win, GLFW_TRUE);
    }

    if (key == GLFW_KEY_TAB)
        key = '\t';
    if (key == GLFW_KEY_ENTER)
        key = '\n';
    if (mods == GLFW_MOD_SUPER) {
        if (key == '[') {
            int f = G->t_q32 & ((1 << 30) - 1);
            G->t_q32 -= f;
            if (f < 1 << 29 && G->t_q32 >= (1 << 30))
                G->t_q32 -= 1 << 30;
        }
        if (key == ']') {
            G->t_q32 &= ~((1 << 30) - 1);
            G->t_q32 += 1 << 30;
        }
        if (key == GLFW_KEY_P) {
            if (!G->playing) {
                // also compile...
                parse_named_patterns_in_source();
            }
            G->playing = !G->playing;
        }
        if (key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD) {
            if (G->playing) {
                G->playing = false;
                G->t_q32 &= ~((1 << 30) - 1);
            } else {
                if (G->t_q32 & (0xffffffffull)) {
                    G->t_q32 &= ~(0xffffffffull);
                } else {
                    G->t_q32 = 0;
                }
            }
        }
        if (key == GLFW_KEY_S) {
            bool compiled = true;
            if (E->editor_type == TAB_CANVAS) {
                bool save_canvas(EditorState * E);
                if (save_canvas(E))
                    set_status_bar(C_OK, "saved canvas");
                else
                    set_status_bar(C_ERR, "failed to save canvas");
            } else {
                FILE *f = fopen("editor.tmp", "w");
                if (f) {
                    fwrite(E->str, 1, stbds_arrlen(E->str), f);
                    fclose(f);
                    if (rename("editor.tmp", E->fname) == 0) {
                        set_status_bar(C_OK, "saved");
                        init_remapping(E);
                        if (E->editor_type == TAB_SHADER) {
                            try_to_compile_shader(E);
                        }
                        if (E->editor_type <= TAB_AUDIO) {
                            parse_named_patterns_in_source();
                            bool need_c_recompile = E->editor_type == TAB_AUDIO;
                            if (E->editor_type == TAB_SHADER) {
                                need_c_recompile = spanstr(E->str, E->str + stbds_arrlen(E->str), "#ifdef C") != NULL;
                            }
                            if (need_c_recompile) {
                                try_to_compile_audio(&E->last_compile_log);
                            }
                        }
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
            if (E->editor_type == TAB_SHADER)
                try_to_compile_shader(E);
            if (E->editor_type <= TAB_AUDIO)
                parse_named_patterns_in_source();
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
    if (G) {
        G->mscrollx += xoffset;
        G->mscrolly += yoffset;
    }
    if (curE->editor_type > TAB_AUDIO)
        return;
    curE->scroll_target_x -= xoffset * curE->font_width;
    curE->scroll_target_y -= yoffset * curE->font_height;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    // printf("char: %d\n", codepoint);
    if (G->ui_alpha_target < 0.5)
        return;
    editor_key(win, curE, codepoint);
    curE->need_scroll_update = true;
}

static void unbind_textures_from_slots(int num_slots) {
    for (int i = 0; i < num_slots; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void init_dynamic_buffer(int n, GLuint *vao, GLuint *vbo, int attrib_count, const uint32_t *attrib_sizes,
                         const uint32_t *attrib_types, const uint32_t *attrib_offsets, uint32_t max_elements, uint32_t stride) {
    glGenVertexArrays(n, vao);
    glGenBuffers(n, vbo);
    for (int i = 0; i < n; i++) {
        glBindVertexArray(vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        if (max_elements > 0)
            glBufferData(GL_ARRAY_BUFFER, stride * max_elements, NULL, GL_DYNAMIC_DRAW); // pre-alloc
        for (int j = 0; j < attrib_count; j++) {
            glEnableVertexAttribArray(j);
            glVertexAttribPointer(j, attrib_sizes[j], attrib_types[j], attrib_types[j] == GL_UNSIGNED_BYTE ? GL_TRUE : GL_FALSE,
                                  stride, (void *)(size_t)attrib_offsets[j]);
            glVertexAttribDivisor(j, 1);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

#include "sample_selector.h"
#include "canvas.h"

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
        editor_click(win, curE, G, mx, my, 0, click_count);
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
        editor_click(win, curE, G, mx, my, -1, click_count);
    }
}

float4 editor_update(EditorState *E, GLFWwindow *win) {
    double mx, my;
    glfwGetCursorPos(win, &mx, &my);
    mx *= retina;
    my *= retina;
    int m0 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    int m1 = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    G->old_mb = G->mb;
    G->old_mx = G->mx;
    G->old_my = G->my;
    G->mx = mx;
    G->my = my;
    G->mb = m0 + m1 * 2;
    if (G->ui_alpha < 0.01f) {
        G->old_mx = G->fbw / 2.f;
        G->old_my = G->fbh / 2.f;
        glfwSetCursorPos(win, G->old_mx / retina, G->old_my / retina);
    }

    G->iTime = glfwGetTime();
    G->cursor_x = E->cursor_x;
    G->cursor_y = E->cursor_y;
    if (m0) {
        editor_click(win, E, G, mx, my, 1, 0);
    }

    int tmw = G->fbw / E->font_width;
    int tmh = G->fbh / E->font_height;
    if (tmw > 512)
        tmw = 512;
    if (tmh > 256 - 8)
        tmh = 256 - 8;

    E->scroll_x += (E->scroll_target_x - E->scroll_x) * 0.1;
    E->scroll_y += (E->scroll_target_y - E->scroll_y) * 0.1;
    if (E->scroll_x < 0)
        E->scroll_x = 0;
    if (E->scroll_y < 0)
        E->scroll_y = 0;

    float tot_lvl = 0.f, tot_tot = 0.f;
    float kick_lvl = 0.f, kick_tot = 0.f;
    float snare_lvl = 0.f, snare_tot = 0.f;
    float hat_lvl = 0.f, hat_tot = 0.f;

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[pbo_index]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    uint32_t *ptr = (uint32_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, textBytes,
                                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (ptr) {
        memset(ptr, 0, textBytes);
        E->intscroll_x = (int)(E->scroll_x / E->font_width);
        E->intscroll_y = (int)(E->scroll_y / E->font_height);
        code_color(E, ptr);

        if (E->editor_type == TAB_SAMPLES) {
            E->font_width = 32;
            E->font_height = E->font_width * 2;
            draw_umap(E, ptr);
        }

        if (status_bar_time > glfwGetTime() - 3.0 && status_bar_color) {
            int x = tmw - strlen(status_bar) + 1;
            print_to_screen(ptr, x, TMH - 21, status_bar_color, false, "%s", status_bar);
        } else {
            status_bar_color = 0;
        }
        if (E->need_scroll_update) {
            E->need_scroll_update = false;
            if (E->cursor_x >= E->intscroll_x + tmw - 4) {
                E->scroll_target_x = (E->cursor_x - tmw + 4) * E->font_width;
            } else if (E->cursor_x < E->intscroll_x + 4) {
                E->scroll_target_x = (E->cursor_x - 4) * E->font_width;
            }
            if (E->cursor_y >= E->intscroll_y + tmh - 4) {
                E->scroll_target_y = (E->cursor_y - tmh + 4) * E->font_height;
            } else if (E->cursor_y < E->intscroll_y + 4) {
                E->scroll_target_y = (E->cursor_y - 4) * E->font_height;
            }
        }
        E->scroll_target_x = clamp(E->scroll_target_x, 0.f, float((E->max_width - tmw + 4) * E->font_width));
        E->scroll_target_y = clamp(E->scroll_target_y, 0.f, float((E->num_lines - tmh + 4) * E->font_height));
        uint32_t slow_scope_start = (scope_pos >> 8);
        uint32_t *scope_dst = ptr + (TMH - 4) * TMW;
        for (int i = 0; i < TMW * 2; ++i) {
            stereo sc = slow_scope[(slow_scope_start - i) & SCOPE_MASK];
            uint16_t l16 = (uint16_t)(clamp(sc.l, -1.f, 1.f) * (32767.f) + 32768.f);
            uint16_t r16 = (uint16_t)(clamp(sc.r, -1.f, 1.f) * (32767.f) + 32768.f);
            scope_dst[i] = (l16 >> 8) | (r16 & 0xff00);
        }
        scope_dst = ptr + (TMH - 2) * TMW;
        uint32_t scope_start = scope_pos - TMW * 8;
        float biggest = 0.f;
        int biggest_offset = 0;
        for (int offset = 0; offset < TMW * 4 - 1; offset += 2) {
            float py = scope[(scope_start + offset) & SCOPE_MASK].l;
            float ny = scope[(scope_start + offset + 1) & SCOPE_MASK].l;
            float dy = ny - py;
            if (ny > 0. && py < 0. && dy > biggest) {
                biggest = dy;
                biggest_offset = offset;
            }
        }
        scope_start += biggest_offset;
        for (int i = 0; i < TMW * 2; ++i) {
            stereo sc = scope[(scope_start + i * 2) & SCOPE_MASK];
            uint16_t l16 = (uint16_t)(clamp(sc.l, -1.f, 1.f) * (32767.f) + 32768.f);
            uint16_t r16 = (uint16_t)(clamp(sc.r, -1.f, 1.f) * (32767.f) + 32768.f);
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
            const float scale = (float)(2.0 * PI) / (float)(FFT_SIZE - 1);
            for (int i = 0; i < FFT_SIZE; ++i) {
                float x = i * scale;
                fft_window[i] = a0 - a1 * cosf(x) + a2 * cosf(2.0f * x);
            }
            for (int i = 0; i < FFT_SIZE / 2; ++i) {
                main_db_smooth[i] = -1e10f;
                probe_db_smooth[i] = -1e10f;
            }
        }

        uint32_t fft_start = scope_pos - FFT_SIZE;
        float fft_buf[2][FFT_SIZE];
        for (int i = 0; i < FFT_SIZE; ++i) {
            stereo sc = scope[(fft_start + i) & SCOPE_MASK];
            stereo pr = probe_scope[(fft_start + i) & SCOPE_MASK];
            fft_buf[0][i] = (sc.l + sc.r) * fft_window[i];
            fft_buf[1][i] = (pr.l + pr.r) * fft_window[i];
        }
        pffft_transform_ordered(fft_setup, fft_buf[0], fft_buf[0], fft_work, PFFFT_FORWARD);
        pffft_transform_ordered(fft_setup, fft_buf[1], fft_buf[1], fft_work, PFFFT_FORWARD);
        scope_dst = ptr + (TMH - 12) * TMW;
        float peak_mag = squared2db(square(0.25f * FFT_SIZE)) - 3.f; // assuming hann, coherent gain is 0.5
        // int min=0, max=0;
        // float minv=1000.f, maxv=-1000.f;
        for (int i = 0; i < 4096; ++i) {
            float binfreq = (i + 0.5f) * (float)SAMPLE_RATE_OUTPUT / (float)FFT_SIZE;
            float binoctave = log2f(binfreq / 1000.f);
            float tilt_db = 4.5f * binoctave;
            float mag_db = squared2db(fft_buf[0][i * 2] * fft_buf[0][i * 2] + fft_buf[0][i * 2 + 1] * fft_buf[0][i * 2 + 1]) -
                           peak_mag + tilt_db;
            float probe_db = squared2db(fft_buf[1][i * 2] * fft_buf[1][i * 2] + fft_buf[1][i * 2 + 1] * fft_buf[1][i * 2 + 1]) -
                             peak_mag + tilt_db;
            float &smoothed_mag_db = main_db_smooth[i];
            float &probe_smoothed_db = probe_db_smooth[i];
            if (mag_db > smoothed_mag_db) {
                smoothed_mag_db = mag_db;
            } else {
                smoothed_mag_db += (mag_db - smoothed_mag_db) * 0.001f;
            }
            if (probe_db > probe_smoothed_db) {
                probe_smoothed_db = probe_db;
            } else {
                probe_smoothed_db += (probe_db - probe_smoothed_db) * 0.001f;
            }
            float peakiness = mag_db + 40.f;
            tot_lvl += peakiness;
            tot_tot++;
            if (binfreq >= 30.f && binfreq < 150.f) {
                kick_lvl += peakiness;
                kick_tot++;
            } else if (binfreq >= 500.f && binfreq < 1000.f) {
                snare_lvl += peakiness;
                snare_tot++;
            } else if (binfreq >= 5000.f) {
                hat_lvl += peakiness;
                hat_tot++;
            }
            // if (magl_db < minv) {minv = magl_db;min = i;}
            // if (magl_db > maxv) {maxv = magl_db;max = i;}
            uint8_t l8 = (uint8_t)(clamp(255.f + mag_db * 6.f, 0.f, 255.f));
            uint8_t r8 = (uint8_t)(clamp(255.f + smoothed_mag_db * 6.f, 0.f, 255.f));
            scope_dst[i] = (l8 << 0) | (r8 << 8);
        }
        tot_lvl = max(0.f, tot_lvl / (tot_tot * 15.f));
        kick_lvl = max(0.f, kick_lvl / (kick_tot * 20.f));
        snare_lvl = max(0.f, snare_lvl / (snare_tot * 15.f));
        hat_lvl = max(0.f, hat_lvl / (hat_tot * 10.f));
        // printf("kick: %f, snare: %f, hat: %f, tot: %f\n", kick_lvl, snare_lvl, hat_lvl, tot_lvl);
        // printf("fft min: %f in bin %d, max: %f in bin %d\n", minv, min, maxv, max);

        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    glBindTexture(GL_TEXTURE_2D, texText);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TMW, TMH, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, (const void *)0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    pbo_index = (pbo_index + 1) % 3;
    check_gl("update text tex");
    return float4{kick_lvl, snare_lvl, hat_lvl, tot_lvl};
}

void on_midi_input(uint8_t data[3], void *user) {
    if (!G)
        return;
    int cc = data[1];
    if (data[0] == 0xb0 && cc < 128) {
        int oldccdata = G->midi_cc[cc];
        int newccdata = data[2];
        G->midi_cc[cc] = newccdata;
        /*
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
            */
    }
    // printf("midi: %02x %02x %02x\n", data[0], data[1], data[2]);
}

// Audio and MIDI initialization
static void init_audio_midi(ma_device *dev) {
    midi_init("Music Thing Modular", on_midi_input, NULL);
    printf("starting audio - device config init\n");
    ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
    cfg.sampleRate = SAMPLE_RATE_OUTPUT;
    cfg.capture.format = cfg.playback.format = ma_format_f32;
    cfg.capture.channels = cfg.playback.channels = 2;
    cfg.dataCallback = audio_cb;
    printf("ma_device_init\n");
    if (ma_device_init(NULL, &cfg, dev) != MA_SUCCESS) {
        die("ma_device_init failed");
    }
    printf("ma_device_start\n");
    if (ma_device_start(dev) != MA_SUCCESS) {
        fprintf(stderr, "ma_device_start failed\n");
        ma_device_uninit(dev);
        exit(3);
    }
    printf("ma_device_start success\n");
}

static GLFWwindow *_win;

bool glfw_get_key(int key) { return _win && glfwGetKey(_win, key) == GLFW_PRESS; }

void update_camera(GLFWwindow *win) {
    camera_state_t *cam = &G->camera;
    memcpy(cam->c_cam2world_old, cam->c_cam2world, sizeof(cam->c_cam2world));
    update_camera_matrix(cam);
    if (!cam->fov)
        cam->fov = 0.4f;
    // cam->focal_distance = length(cam->c_lookat - cam->c_pos);
    _win = win;
    if (g_frame_update_func)
        g_frame_update_func(glfw_get_key, G);
    _win = NULL;
    if (!dot(cam->c_cam2world_old[0], cam->c_cam2world_old[0])) {
        memcpy(cam->c_cam2world_old, cam->c_cam2world, sizeof(cam->c_cam2world));
    }
}

static void render_taa_pass(GLuint taa_pass, GLuint *fbo, GLuint *texFPRT, int num_fprts, uint32_t iFrame, GLuint vao) {
    glUseProgram(taa_pass);
    bind_texture_to_slot(taa_pass, 0, "uFP", texFPRT[2], GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    bind_texture_to_slot(taa_pass, 1, "uFP_prev", texFPRT[(iFrame + 1) % 2], GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uniformMatrix4fv(taa_pass, "c_cam2world", 1, GL_FALSE, (float *)G->camera.c_cam2world);
    uniformMatrix4fv(taa_pass, "c_cam2world_old", 1, GL_FALSE, (float *)G->camera.c_cam2world_old);
    uniform1f(taa_pass, "fov", G->camera.fov);
    draw_fullscreen_pass(fbo[iFrame % 2], RESW, RESH, vao);
}

static void render_bloom_downsample(GLuint bloom_pass, GLuint *fbo, GLuint *texFPRT, uint32_t iFrame, int num_bloom_mips,
                                    int num_fprts, GLuint vao) {
    for (int i = 0; i < num_bloom_mips; ++i) {
        int resw = RESW >> (i + 1);
        int resh = RESH >> (i + 1);
        glUseProgram(bloom_pass);
        set_bloom_uniforms(bloom_pass, resw, resh, BLOOM_KERNEL_SIZE_DOWNSAMPLE, BLOOM_FADE_FACTOR);
        bind_texture_to_slot(bloom_pass, 0, "uFP", texFPRT[i ? (i + num_fprts - 1) : (iFrame % 2)], GL_LINEAR);
        draw_fullscreen_pass(fbo[i + num_fprts], resw, resh, vao);
    }
}

static void render_bloom_upsample(GLuint bloom_pass, GLuint *fbo, GLuint *texFPRT, int num_bloom_mips, int num_fprts, GLuint vao) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBlendFunc(GL_DST_ALPHA, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);

    for (int i = num_bloom_mips - 2; i >= 0; --i) {
        int resw = RESW >> (i + 1);
        int resh = RESH >> (i + 1);
        glUseProgram(bloom_pass);
        set_bloom_uniforms(bloom_pass, resw, resh, BLOOM_KERNEL_SIZE_UPSAMPLE, BLOOM_FADE_FACTOR * BLOOM_SPIKEYNESS);
        bind_texture_to_slot(bloom_pass, 0, "uFP", texFPRT[i + num_fprts + 1], GL_LINEAR);
        draw_fullscreen_pass(fbo[i + num_fprts], resw, resh, vao);
    }
    glDisable(GL_BLEND);
}

struct texture_t {
    const char *key;
    const char *url;
    int texture;
} *textures = NULL;

bool skies_loaded = false;

GLuint load_texture(const char *key, int filter_mode = GL_LINEAR, int wrap_mode = GL_CLAMP_TO_EDGE) {
    if (!skies_loaded) {
        skies_loaded = true;
        char *json = load_file("assets/skies.json");
        sj_Reader r = sj_reader(json, stbds_arrlen(json));
        sj_Value outer_obj = sj_read(&r);
        sj_Value key, val;
        while (sj_iter_object(&r, outer_obj, &key, &val)) {
            if (val.type == SJ_STRING) {
                const char *k = stbstring_from_span(key.start, key.end, 0);
                const char *v = stbstring_from_span(val.start, val.end, 0);
                texture_t s = {k, v, -1};
                stbds_shputs(textures, s);
            }
        }
        printf("loaded list of skies\n");
    }
    texture_t *tex = stbds_shgetp_null(textures, key);
    if (!tex) {
        char *k = stbstring_from_span(key);
        texture_t t = {k, k, -1};
        stbds_shputs(textures, t);
        tex = stbds_shgetp_null(textures, key);
    }
    if (tex->texture >= 0)
        return tex->texture;
    const char *fname = fetch_to_cache(tex->url, 1);
    if (!fname) {
        fprintf(stderr, "failed to fetch texture %s\n", tex->url);
        tex->texture = 0;
        return 0;
    }
    int w, h, n;
    void *img = NULL;
    int hdr = stbi_is_hdr(fname);
    if (hdr) {
        img = stbi_loadf(fname, &w, &h, &n, 4);
        if (!img) {
            fprintf(stderr, "failed to load hdr texture %s\n", fname);
            tex->texture = 0;
            return 0;
        }
        float *fimg = (float *)img;
#define MAX_FLOAT16 65504.f
        for (int i = 0; i < w * h * 4; i++) {
            if (fimg[i] >= MAX_FLOAT16 || isinf(fimg[i]))
                fimg[i] = MAX_FLOAT16;
        }
    } else {
        img = stbi_load(fname, &w, &h, &n, 4);
        if (!img) {
            fprintf(stderr, "failed to load ldr texture %s\n", fname);
            tex->texture = 0;
            return 0;
        }
    }
    GLuint texi = gl_create_texture(filter_mode, wrap_mode);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, img);
    if (hdr) {
        int nummips = 6;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, nummips - 1);
        float4 *srcmip = (float4 *)img;
        assert(size_t(img) % 16 == 0);
        float4 *dstmipmem[2] = {(float4 *)malloc(w / 4 * h / 4 * 4 * sizeof(float)),
                                (float4 *)malloc(w / 2 * h / 2 * 4 * sizeof(float))};
        for (int mip = 1; mip < nummips; ++mip) {
            float4 *dstmip = dstmipmem[mip % 2];
            int srcw = w >> (mip - 1);
            int srch = h >> (mip - 1);
            int mipw = w >> mip;
            int miph = h >> mip;
            for (int y = 0; y < miph; y++) {
                float yweights[6] = {0.25, 0.5, 1., 1., 0.5, 0.25};
                for (int yy = 0; yy < 6; ++yy) {
                    yweights[yy] *= max(0.f, sinf((y * 2 + yy - 2 + 0.5f) * PI / srch));
                }
                for (int x = 0; x < mipw; x++) {
                    float4 tot = {0.f, 0.f, 0.f, 0.f};
                    for (int yy = -2; yy < 4; ++yy) {
                        int ysrc = clamp(y * 2 + yy, 0, srch - 1) * srcw;
                        float weight = yweights[yy + 2]; // half arsed attempt to account for the stretch - this rectangular kernel
                                                         // aint right but hey. its ok.
                        tot += srcmip[x * 2 + ysrc] * weight;
                        tot += srcmip[x * 2 + 1 + ysrc] * weight;
                        weight *= 0.5f;
                        tot += srcmip[((x * 2 + 2) & (srcw - 1)) + ysrc] * weight;
                        tot += srcmip[((x * 2 - 1) & (srcw - 1)) + ysrc] * weight;
                        weight *= 0.5f;
                        tot += srcmip[((x * 2 + 3) & (srcw - 1)) + ysrc] * weight;
                        tot += srcmip[((x * 2 - 2) & (srcw - 1)) + ysrc] * weight;
                    }
                    tot *= 1.f / tot.w;
                    dstmip[x + y * mipw] = tot;
                }
            }
            // set it to opengl
            glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA16F, mipw, miph, 0, GL_RGBA, GL_FLOAT, dstmip);
            srcmip = dstmip;
        }
        free(dstmipmem[0]);
        free(dstmipmem[1]);
    }
    stbi_image_free(img);
    check_gl("load texture");
    glBindTexture(GL_TEXTURE_2D, 0);
    tex->texture = texi;
    return texi;
}


// typedef struct sphere_t {
//     float4 pos_rad;
//     float4 oldpos_rad;
//     float4 colour;
// } sphere_t;

#define MAX_SPHERES 64 // must be a multiple of 256
#define NUM_BVH_LEVELS 4
float4 spheres[MAX_SPHERES * 3 + (MAX_SPHERES / 4 + MAX_SPHERES / 16 + MAX_SPHERES / 64 + MAX_SPHERES / 256) * 2];

static inline float4 sphere_bbox_min(float4 pos_rad) {
    return pos_rad - pos_rad.wwww; // w will be 0, which is fine;
}
static inline float4 sphere_bbox_max(float4 pos_rad) {
    return pos_rad + pos_rad.wwww; // w will be double radius, which is silly.
}

static void update_spheres(void) {
    float iTime = G->iTime;
    for (int i = 0; i < MAX_SPHERES; i++) {
        int2 xy = (int2)unmorton2(i);
        spheres[i * 3 + 1] = spheres[i * 3 + 0];
        float xx = 0.25f * sinf(iTime + xy.y);
        float zz = 0.25f * sinf(iTime * 1.1f + xy.x);
        float yy = 0.25f * sinf(iTime * 1.2f + (xy.x + xy.y));
        float glo = max(0.f, 30000.f * (sinf(iTime * 0.3f + 0.3 * (xy.x * xy.y)) - 0.99f));
        float ww = 0.125f + 0.125f * sinf(iTime * 1.3f + (xy.x - xy.y));
        spheres[i * 3 + 0] = float4{(float)xy.x * 0.5f + xx, yy, (float)xy.y * 0.5f + zz, ww};
        if (spheres[i * 3 + 1].w <= 0.f)
            spheres[i * 3 + 1] = spheres[i * 3 + 0]; // new sphere.
        spheres[i * 3 + 2] = float4{0.1f, 0.1f, 0.1f, glo};
    }
    // spheres[0].pos_rad = float4{1.5f,-10.5f,1.5f,10.f};
    //  for (int i= 0; i<10;++i)  {
    //      spheres[rndint(MAX_SPHERES)*3+2] = float4{0.f, 0.f, 0.f, 0.f};
    //      spheres[rndint(MAX_SPHERES)*3+2] = float4{1.f, 0.2f, 0.1f, 10.f};
    //  }

    // compute bounding boxes bottom to top
    // level 0: min/max over motion blurred spheres.
    float4 *dst = spheres + MAX_SPHERES * 3;
    const float4 *src = spheres;
    for (int i = 0; i < MAX_SPHERES; i += 4) {
        float4 minp = min(sphere_bbox_min(src[0]), sphere_bbox_min(src[1]));
        float4 maxp = max(sphere_bbox_max(src[0]), sphere_bbox_max(src[1]));
        src += 3;
        for (int j = 1; j < 4; j++) {
            minp = min(minp, min(sphere_bbox_min(src[0]), sphere_bbox_min(src[1])));
            maxp = max(maxp, max(sphere_bbox_max(src[0]), sphere_bbox_max(src[1])));
            src += 3;
        }
        *dst++ = minp;
        *dst++ = maxp;
    }
    // level 1-n: min/max over child bvh nodes.
    for (int level = 1; level < NUM_BVH_LEVELS; level++) {
        int n = MAX_SPHERES >> (level * 2 + 2);
        for (int i = 0; i < n; i++) {
            *dst++ = min(min(min(src[0], src[2]), src[4]), src[6]);
            *dst++ = max(max(max(src[1], src[3]), src[5]), src[7]);
            src += 8;
        }
    }
}

inline float atof_default(const char *s, float def) {
    if (!s)
        return def;
    char *end = NULL;
    float f = strtod(s, &end);
    if (end == s)
        return def;
    return f;
}

void set_aradjust(GLuint shader, int fbw, int fbh) {
    float output_ar = fbw / float(fbh);
    float input_ar = RESW / float(RESH);
    float scale = output_ar / input_ar;
    if (scale > 1.f) { // output is wider than input - black bars at sides
        uniform2f(shader, "uARadjust", scale, 1.f);
    } else { // black bars at top and bottom
        uniform2f(shader, "uARadjust", 1.f, 1.f / scale);
    }
    check_gl("set_aradjust");
}

GLuint compile_fs_with_user_prefix(const char *fs_suffix) {
    size_t n = strlen(kFS_user_prefix) + strlen(fs_suffix) + 1;
    char *fs_str = (char *)malloc(n);
    snprintf(fs_str, n, "%s%s", kFS_user_prefix, fs_suffix);
    GLuint fs = compile_shader(NULL, GL_FRAGMENT_SHADER, fs_str);
    free(fs_str);
    return fs;
}

shader_param_t param_cc[8];

void error_callback(int error, const char *description) { fprintf(stderr, "GLFW error %d: %s\n", error, description); }

int main(int argc, char **argv) {
    printf(COLOR_CYAN "ginkgo" COLOR_RESET " - " __DATE__ " " __TIME__ "\n");
#if USING_ASAN
    printf(COLOR_RED "Address sanitizer enabled\n" COLOR_RESET);
#endif
    void install_crash_handler(void);
    // install_crash_handler();
    glfwSetErrorCallback(error_callback);
    if (!glfwInit())
        die("glfwInit failed");
    int count = 0;
    GLFWmonitor **mons = glfwGetMonitors(&count);
    printf(COLOR_CYAN "%d" COLOR_RESET " monitors found\n", count);

    if (count > 1) {
        GLFWmonitor *primon = glfwGetPrimaryMonitor();
        for (int i = 0; i < count; i++) {
            const GLFWvidmode *vm = glfwGetVideoMode(mons[i]);
            const char *name = glfwGetMonitorName(mons[i]);
            printf(COLOR_YELLOW "  %d: %dx%d @ %dHz - " COLOR_CYAN "%s" COLOR_RED "%s" COLOR_RESET "\n", i, vm->width, vm->height,
                   vm->refreshRate, name, mons[i] == primon ? " (primary)" : "");
        }
    }

    int primon_idx = -1;
    int secmon_idx = -1;
    bool prefetch = false;
    G->ui_alpha_target = 1.f;
    load_settings(argc, argv, &primon_idx, &secmon_idx, &prefetch);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_sampler(prefetch);

    void test_minipat(void);
    test_minipat();
    // return 0;

    GLFWwindow *win = gl_init(primon_idx, secmon_idx);

#ifdef __APPLE__
    void *nswin = glfwGetCocoaWindow(win);
    mac_touch_init(nswin);
#endif

    vs = compile_shader(NULL, GL_VERTEX_SHADER, kVS);
    fs_ui = compile_fs_with_user_prefix(kFS_ui_suffix);
    fs_bloom = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_bloom);
    fs_taa = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_taa);
    fs_secmon = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_secmon);
    ui_pass = link_program(vs, fs_ui);
    bloom_pass = link_program(vs, fs_bloom);
    taa_pass = link_program(vs, fs_taa);
    secmon_pass = link_program(vs, fs_secmon);
    glDeleteShader(fs_ui);
    glDeleteShader(fs_bloom);
    glDeleteShader(fs_taa);
    GLuint vs_fat = compile_shader(NULL, GL_VERTEX_SHADER, kVS_fat);
    GLuint fs_fat = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_fat);
    GLuint fat_prog = link_program(vs_fat, fs_fat);
    glDeleteShader(fs_fat);
    glDeleteShader(vs_fat);

    GLuint vs_stroke = compile_shader(NULL, GL_VERTEX_SHADER, kVS_stroke);
    GLuint fs_stroke = compile_shader(NULL, GL_FRAGMENT_SHADER, kFS_stroke);
    stroke_prog = link_program(vs_stroke, fs_stroke);
    glDeleteShader(fs_stroke);
    glDeleteShader(vs_stroke);

    load_file_into_editor(&tabs[TAB_SHADER]);
    load_file_into_editor(&tabs[TAB_AUDIO]);
    try_to_compile_shader(&tabs[TAB_SHADER]);
    parse_named_patterns_in_source();
    // audio needs to be compiled after dsp is running
    parse_error_log(&tabs[TAB_AUDIO]);
    bool load_canvas(EditorState * E);
    load_canvas(&tabs[TAB_CANVAS]);

    texFont = load_texture("assets/font_sdf.png");

    // GLuint paper_diff = load_texture("assets/textures/paper-rough-1-DIFFUSE.png", GL_LINEAR, GL_REPEAT);
    // GLuint paper_disp = load_texture("assets/textures/paper-rough-1-DISP.png", GL_LINEAR, GL_REPEAT);
    // GLuint paper_norm = load_texture("assets/textures/paper-rough-1-NORM.png", GL_LINEAR, GL_REPEAT);

    texText = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, TMW, TMH, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    check_gl("alloc text tex");
    glBindTexture(GL_TEXTURE_2D, 0);

#define NUM_BLOOM_MIPS 8
#define NUM_FPRTS 3
    GLuint texFPRT[NUM_FPRTS + NUM_BLOOM_MIPS];
    GLuint fbo[NUM_FPRTS + NUM_BLOOM_MIPS];
    setup_framebuffers_and_textures(texFPRT, fbo, NUM_BLOOM_MIPS, NUM_FPRTS);

    glGenBuffers(3, pbos);
    for (int i = 0; i < 3; ++i) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[i]);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    // fat line init
    GLuint fatvao[2] = {}, fatvbo[2] = {};
    static const uint32_t attrib_sizes[5] = {2, 2, 2, 1, 4};
    static const uint32_t attrib_types[5] = {GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_INT, GL_UNSIGNED_BYTE};
    static const uint32_t attrib_offsets[5] = {offsetof(line_t, p0x), offsetof(line_t, p1x), offsetof(line_t, width),
                                               offsetof(line_t, character), offsetof(line_t, col)};
    init_dynamic_buffer(2, fatvao, fatvbo, 5, attrib_sizes, attrib_types, attrib_offsets, MAX_LINES, sizeof(line_t));
    check_gl("init fat line buffer post");

    init_stroke_buffer();

    // sphere init
    GLuint spheretbotex;
    glGenTextures(1, &spheretbotex);

    GLuint spheretbos[2];
    glGenBuffers(2, spheretbos);
    check_gl("init sphere buffer post");

    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    pump_wave_load_requests_main_thread();
    usleep(1000);
    ma_device dev;
    init_audio_midi(&dev);

    try_to_compile_audio(&tabs[TAB_AUDIO].last_compile_log);

    double start_time = glfwGetTime();
    double prev_frame_time = 0.;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        glfwGetFramebufferSize(win, &G->fbw, &G->fbh);
        update_multitouch_dragging();

        double iTime = glfwGetTime() - start_time;
        double frame_time = max(1. / 240., iTime - prev_frame_time);
        prev_frame_time = iTime;
        static uint32_t iFrame = 0;

        glDisable(GL_DEPTH_TEST);
        static float4 lvl_smooth = {0.f, 0.f, 0.f, 0.f};
        float4 lvl_peaks = editor_update(curE, win);
        float smooth_decay = powf(0.25f, frame_time * 10.f);
        lvl_smooth = max(lvl_smooth * smooth_decay, lvl_peaks);
        stbds_arrpush(tabs[TAB_SHADER].str, 0);
        const char *s = tabs[TAB_SHADER].str;
        const char *e = tabs[TAB_SHADER].str + stbds_arrlen(tabs[TAB_SHADER].str);
        const char *sky_name = strstr(s, "// sky ");
        const char *tex_name = strstr(s, "// tex ");
        const char *want_taa_str = strstr(s, "// taa");
        if (want_taa_str && want_taa_str[6] == ' ' && want_taa_str[7] == '0')
            want_taa_str = NULL;
        bool want_taa = (want_taa_str != NULL) && (curE != &tabs[TAB_CANVAS]);
        const char *aperture_str = strstr(s, "// aperture ");
        if (aperture_str)
            G->camera.aperture = atof_default(aperture_str + 11, G->camera.aperture);
        const char *focal_distance_str = strstr(s, "// focal_distance ");
        if (focal_distance_str)
            G->camera.focal_distance = atof_default(focal_distance_str + 17, G->camera.focal_distance);
        const char *fov_str = strstr(s, "// fov ");
        if (fov_str)
            G->camera.fov = atof_default(fov_str + 7, G->camera.fov);
        stbds_arrpop(tabs[TAB_SHADER].str);
        if (sky_name) {
            s = sky_name + 6;
            while (s < e && *s != '\n' && isspace(*s))
                ++s;
            const char *spans = s;
            while (s < e && *s != '\n' && !isspace(*s))
                ++s;
            sky_name = temp_cstring_from_span(spans, s);
            if (sky_name) {
               skytex = load_texture(sky_name);
            }
        }
        if (tex_name) {
            s = tex_name + 6;
            while (s < e && *s != '\n' && isspace(*s))
                ++s;
            const char *spans = s;
            while (s < e && *s != '\n' && !isspace(*s))
                ++s;
            tex_name = temp_cstring_from_span(spans, s);
            if (tex_name) {
                textex = load_texture(tex_name);
            }
        }

        if (want_taa) { // TODO: only update spheres if they wanted them
            glBindBuffer(GL_TEXTURE_BUFFER, spheretbos[iFrame % 2]);
            glBufferData(GL_TEXTURE_BUFFER, sizeof(spheres), nullptr, GL_STREAM_DRAW); // orphan
            void *p = glMapBufferRange(GL_TEXTURE_BUFFER, 0, sizeof(spheres),
                                       GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            // spheres[0].pos_rad.w = fabsf(sinf(iTime)) * 0.5f;
            update_spheres();
            memcpy(p, spheres, sizeof(spheres));
            glUnmapBuffer(GL_TEXTURE_BUFFER);
            glBindTexture(GL_TEXTURE_BUFFER, spheretbotex);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, spheretbos[iFrame % 2]);
        }
        update_camera(win);
        //////////////////////// USER PASS
        if (!user_pass) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo[want_taa ? 2 : iFrame % 2]);
            glViewport(0, 0, RESW, RESH);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            glUseProgram(user_pass);
            static double prev_time = 0.;
            static double cur_time = G->t;
            static bool was_playing = false;
            double dt = 0.;
            bool reset_integration = false;
            if (G->playing) {
                prev_time = cur_time;
                cur_time = G->t;
                dt = cur_time - prev_time;
                if (!was_playing && dt < 0.)
                    reset_integration = true;
                if (dt > 1. / 30.)
                    dt = 1. / 30.;
                if (dt < 0.)
                    dt = 0.;
            }
            was_playing = G->playing;
            for (int i = 0; i < stbds_shlen(G->patterns_map); i++) {
                pattern_t *p = &G->patterns_map[i];
                if (p->uniform_idx != -1) {
                    hap_t dst[8];
                    hap_span_t hs = p->make_haps({dst, dst + 8}, 8, iTime, G->t);
                    float value = -1e10f;
                    for (hap_t *h = hs.s; h < hs.e; h++) {
                        if (h->has_param(P_NUMBER))
                            value = max(value, h->get_param(P_NUMBER, 0.f));
                    }
                    if (value == -1e10f)
                        value = p->shader_param.value;
                    p->shader_param.update(value, dt, reset_integration);
                    uniform4f(user_pass, p->key + 1, p->shader_param.value, p->shader_param.old_value,
                              p->shader_param.integrated_value, p->shader_param.old_integrated_value);
                }
            }
            float ccs[8 * 4];
            for (int i = 0; i < 8; ++i) {
                param_cc[i].update(cc(i), dt, reset_integration);
                ccs[i * 4 + 0] = param_cc[i].value;
                ccs[i * 4 + 1] = param_cc[i].old_value;
                ccs[i * 4 + 2] = param_cc[i].integrated_value;
                ccs[i * 4 + 3] = param_cc[i].old_integrated_value;
            }
            uniform4fv(user_pass, "cc", 8, (float *)ccs);
            uniform2i(user_pass, "uScreenPx", RESW, RESH);
            uniform1f(user_pass, "iTime", (float)iTime);
            uniform1f(user_pass, "t", (float)G->t);
            uniform1ui(user_pass, "iFrame", iFrame);
            uniform2i(user_pass, "uScreenPx", G->fbw, G->fbh);
            uniform2i(user_pass, "uFontPx", curE->font_width, curE->font_height);
            uniform1i(user_pass, "status_bar_size", status_bar_color ? 1 : 0);
            uniform2f(user_pass, "scroll", curE->scroll_x - curE->intscroll_x * curE->font_width,
                      curE->scroll_y - curE->intscroll_y * curE->font_height);
            uniform1f(user_pass, "ui_alpha", G->ui_alpha);
            uniform4f(user_pass, "levels", lvl_peaks.x, lvl_peaks.y, lvl_peaks.z, lvl_peaks.w);
            uniform4f(user_pass, "levels_smooth", lvl_smooth.x, lvl_smooth.y, lvl_smooth.z, lvl_smooth.w);
            bind_texture_to_slot(user_pass, 1, "uFont", texFont, GL_LINEAR);
            bind_texture_to_slot(user_pass, 2, "uText", texText, GL_NEAREST);
            bind_texture_to_slot(user_pass, 0, "uFP", texFPRT[(iFrame + 1) % 2], GL_NEAREST);
            bind_texture_to_slot(user_pass, 3, "uSky", (skytex<0)?0:skytex, GL_LINEAR);
            bind_texture_to_slot(user_pass, 4, "uTex", (textex<0)?0:textex, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 4, "uPaperDiff", paper_diff, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 5, "uPaperDisp", paper_disp, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 6, "uPaperNorm", paper_norm, GL_LINEAR);
            uniform1i(user_pass, "uSpheres", 7);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_BUFFER, spheretbotex);

            uniformMatrix4fv(user_pass, "c_cam2world", 1, GL_FALSE, (float *)G->camera.c_cam2world);
            uniformMatrix4fv(user_pass, "c_cam2world_old", 1, GL_FALSE, (float *)G->camera.c_cam2world_old);
            uniform4f(user_pass, "c_lookat", G->camera.c_lookat.x, G->camera.c_lookat.y, G->camera.c_lookat.z,
                      G->camera.focal_distance);
            uniform1f(user_pass, "fov", G->camera.fov);
            uniform1f(user_pass, "focal_distance", G->camera.focal_distance);
            uniform1f(user_pass, "aperture", G->camera.aperture);

            draw_fullscreen_pass(fbo[want_taa ? 2 : iFrame % 2], RESW, RESH, vao);
            unbind_textures_from_slots(7);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_BUFFER, 0);
        }
        if (want_taa)
            render_taa_pass(taa_pass, fbo, texFPRT, NUM_FPRTS, iFrame, vao);
        render_bloom_downsample(bloom_pass, fbo, texFPRT, iFrame, NUM_BLOOM_MIPS, NUM_FPRTS, vao);
        render_bloom_upsample(bloom_pass, fbo, texFPRT, NUM_BLOOM_MIPS, NUM_FPRTS, vao);

        GLsync fprtReady = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        // ui pass //////////////////
        glUseProgram(ui_pass);
        bind_texture_to_slot(ui_pass, 0, "uFP", texFPRT[iFrame % 2], GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

        bind_texture_to_slot(ui_pass, 1, "uFont", texFont, GL_LINEAR);
        bind_texture_to_slot(ui_pass, 2, "uText", texText, GL_NEAREST);
        bind_texture_to_slot(ui_pass, 3, "uBloom", texFPRT[NUM_FPRTS], GL_LINEAR);
        set_aradjust(ui_pass, G->fbw, G->fbh);

        uniform2i(ui_pass, "uScreenPx", G->fbw, G->fbh);
        uniform2i(ui_pass, "uFontPx", curE->font_width, curE->font_height);
        uniform1i(ui_pass, "status_bar_size", status_bar_color ? 1 : 0);
        uniform1f(ui_pass, "iTime", (float)iTime);
        uniform2f(ui_pass, "scroll", curE->scroll_x - curE->intscroll_x * curE->font_width,
                  curE->scroll_y - curE->intscroll_y * curE->font_height);
        uniform1f(ui_pass, "ui_alpha", G->ui_alpha);
        // if there's a second monitor, we can afford to fade the render a bit more
        // to make the code more readable.
        // the first tab (shader) is faded less.
        float fade_render = lerp(1.f, winFS ? 0.5f : (curE == &tabs[TAB_SHADER] ? 1.f : 0.8f), G->ui_alpha);
        uniform1f(ui_pass, "fade_render", fade_render);

        float f_cursor_x = (curE->cursor_x - curE->intscroll_x) * curE->font_width;
        float f_cursor_y = (curE->cursor_y - curE->intscroll_y) * curE->font_height;
        uniform4f(ui_pass, "cursor", f_cursor_x, f_cursor_y, curE->prev_cursor_x, curE->prev_cursor_y);
        curE->prev_cursor_x += (f_cursor_x - curE->prev_cursor_x) * CURSOR_SMOOTH_FACTOR;
        curE->prev_cursor_y += (f_cursor_y - curE->prev_cursor_y) * CURSOR_SMOOTH_FACTOR;

        draw_fullscreen_pass(0, G->fbw, G->fbh, vao);
        unbind_textures_from_slots(5);

        //////////////////////////////

        if (curE->editor_type == TAB_CANVAS) {
            curE->font_width = 32;
            curE->font_height = curE->font_width * 2;
            draw_canvas(curE);
        }

#define MOUSE_LEN 8
#define MOUSE_MASK (MOUSE_LEN - 1)
        static float mxhistory[MOUSE_LEN], myhistory[MOUSE_LEN];
        int mpos = (iFrame % MOUSE_LEN);
        mxhistory[mpos] = G->mx;
        myhistory[mpos] = G->my;
        int numlines = min(iFrame, MOUSE_LEN - 1u);
        for (int i = 0; i < numlines; i++) {
            float p0x = mxhistory[(mpos - i) & MOUSE_MASK];
            float p0y = myhistory[(mpos - i) & MOUSE_MASK];
            float p1x = mxhistory[(mpos - i - 1) & MOUSE_MASK];
            float p1y = myhistory[(mpos - i - 1) & MOUSE_MASK];
            float len = 50.f + sqrtf(square(p0x - p1x) + square(p0y - p1y));
            int alpha = (int)clamp(len * G->ui_alpha, 0.f, 255.f);
            // red is in the lsb. premultiplied alpha so alpha=0 == additive
            uint32_t col = (alpha << 24) | ((alpha >> 0) << 16) | ((alpha >> 0) << 8) | (alpha >> 0);
            if (i == 0)
                col = 0;
            if (alpha > 0)
                add_line(p0x, p0y, p1x, p1y, col, 17.f - i);
        }

        static const uint32_t cc_cols[] = {
            0x3344ee, 0x3344ee, 0x4477ee, 0x4477ee, 0x33ccff, 0x33ccff, 0xffffee, 0xffffee,
        };
        float cc_bar_x = G->fbw - curE->font_width * 16.f;
        float cc_bar_height = curE->font_height;
        for (int i = 0; i < 8; ++i) {
            float x = cc_bar_x + (i - 7.5f) * 30.f;
            float y = G->fbh;
            float y2 = y - G->midi_cc[i + 0x10] * cc_bar_height / 128.f;
            add_line(x, y - cc_bar_height, x, y, 0x3f000000 | ((cc_cols[i] >> 2) & 0x3f3f3f),
                     -20.f); // negative width is square cap
            add_line(x, y, x, y2, 0x3f000000 | (cc_cols[i]), -20.f);
        }

        draw_logo(iTime - start_time);
        // test_svf_gain();

        // fat line draw
        if (line_count > 0) {
            // line_t lines[1] = {{300.f,100.f,G->mx,G->my,0xffff00ff,54.f}};
            GLsizeiptr bytes = (GLsizeiptr)(line_count * sizeof(line_t));
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glBlendEquation(GL_FUNC_ADD);
            glBindVertexArray(fatvao[iFrame % 2]);
            glBindBuffer(GL_ARRAY_BUFFER, fatvbo[iFrame % 2]);
            glFinish(); // this seems to clean up some intermittent glitching to do with the fat line buffer. weird? shouldnt be
                        // needed.
            glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, lines);
            glUseProgram(fat_prog);
            uniform2f(fat_prog, "fScreenPx", (float)G->fbw, (float)G->fbh);
            uniform2i(fat_prog, "uFontPx", curE->font_width, curE->font_height);
            bind_texture_to_slot(fat_prog, 0, "uFont", texFont, GL_LINEAR);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, line_count);
            glBindVertexArray(0);
            unbind_textures_from_slots(1);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDisable(GL_BLEND);
            line_count = 0;
            check_gl("fat line draw post");
        }

        G->ui_alpha += (G->ui_alpha_target - G->ui_alpha) * 0.1;

        glfwSwapBuffers(win);
        if (winFS) {
            check_gl("pre second mon");

            glfwMakeContextCurrent(winFS);
            int sw, sh;
            glfwGetFramebufferSize(winFS, &sw, &sh);
            glViewport(0, 0, sw, sh);
            glClearColor(1.f, 0.f, 1.f, 1.f);
            // glClear(GL_COLOR_BUFFER_BIT);
            glWaitSync(fprtReady, 0, GL_TIMEOUT_IGNORED);
            glUseProgram(secmon_pass);
            bind_texture_to_slot(secmon_pass, 0, "uFP", texFPRT[iFrame % 2], GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            bind_texture_to_slot(secmon_pass, 1, "uBloom", texFPRT[NUM_FPRTS], GL_LINEAR);
            set_aradjust(secmon_pass, sw, sh);
            glBindVertexArray(vaoFS);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            unbind_textures_from_slots(2);
            glBindVertexArray(0);
            glfwSwapBuffers(winFS);
            glfwMakeContextCurrent(win);
            check_gl("post second mon");
        }
        iFrame++;

        /*
        // poll for changes in the audio file
        static time_t last = 0;
        struct stat st;
        if (stat(tabs[TAB_AUDIO].fname, &st) == 0 && st.st_mtime != last) {
            last = st.st_mtime;
            try_to_compile_audio(tabs[TAB_AUDIO].fname, &tabs[TAB_AUDIO].last_compile_log);
            parse_error_log(&tabs[TAB_AUDIO]);
        }
            */

        double t = G->t;
        int bar = (int)t;
        int beat = (int)((t - bar) * 64);
        set_status_bar(G->playing ? 0x04cfff00 : 0x0222aaa00, " %.1f%% | %x.%02x  ", G->cpu_usage_smooth * 100.f, bar, beat);

        // pump wave load requests
        pump_wave_load_requests_main_thread();
        // usleep(100000);
    }

    dump_settings();
    ma_device_stop(&dev);
    ma_device_uninit(&dev);

    if (winFS) {
        glDeleteVertexArrays(1, &vaoFS);
        glfwDestroyWindow(winFS);
        winFS = NULL;
    }
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texText);
    glDeleteTextures(1, &texFont);
    glDeleteTextures(NUM_FPRTS + NUM_BLOOM_MIPS, texFPRT);
    glDeleteFramebuffers(NUM_FPRTS + NUM_BLOOM_MIPS, fbo);
    glDeleteProgram(ui_pass);
    glDeleteBuffers(3, pbos);

    glfwDestroyWindow(win);
    glfwTerminate();
    curl_global_cleanup();
    return 0;
}
