
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
#include "http_fetch.h"
#include "extvector.h"
#include "morton.h"
#include "json.h"
#include "logo.h"
#include "crash.h"

#define RESW 1920
#define RESH 1080

// Named constants for magic numbers
#define BLOOM_FADE_FACTOR (1.f / 16.f)
#define BLOOM_SPIKEYNESS 0.5f
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
    uint32_t col;
    float width;
} line_t;

#define MAX_LINES (1 << 15)
uint32_t line_count = 0;
line_t lines[MAX_LINES];

void add_line(float p0x, float p0y, float p1x, float p1y, uint32_t col, float width) {
    if (line_count >= MAX_LINES)
        return;
    lines[line_count] = (line_t){p0x, p0y, p1x, p1y, col, width};
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
        float hw = abs(0.5 * in_width);
        vec2 dir = p2-p1;
        float len = length(dir);
        vec2 t = (len<1e-6) ? vec2(hw, 0.0) : dir * -(hw/len);
        vec2 n = vec2(-t.y, t.x);
        // 0    1/5
        // 2/4    3
        int corner = gl_VertexID % 6;
        vec2 p = p1;
        float vhw = hw; 
        if (in_width < 0.) { vhw = 0.; t=vec2(0.); } // square cap if in_width is negative
        vec2 uv = vec2(-hw,-vhw);
        if ((corner&1)==1) { t=-t; p = p2; uv.y = len+vhw;}
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
uvec4 seed; 
uniform sampler2D uFP; 
uniform ivec2 uScreenPx;
uniform sampler2D uFont; 
uniform usampler2D uText; 

uniform vec2 scroll; 
uniform vec4 cursor; 
uniform float ui_alpha;
uniform ivec2 uFontPx;
uniform int status_bar_size;
uniform sampler2D uBloom;
uniform vec2 uARadjust;


uniform sampler2D uSky;
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
uvec4 pcg4d() {
    uvec4 v = seed * 1664525u + 1013904223u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    v ^= v >> 16u;
    v.x += v.y*v.w; v.y += v.z*v.x; v.z += v.x*v.y; v.w += v.y*v.z;
    seed += v;
    return v;
}
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
vec4 rnd4() { return vec4(pcg4d()) * (1.f / 4294967296.f); }
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

float aabb_intersect(vec3 ro, vec3 inv_rd, vec3 boxmin, vec3 boxmax) {
    vec3 t0 = (boxmin - ro) * inv_rd;
    vec3 t1 = (boxmax - ro) * inv_rd;
    vec3 tmin_v = min(t0, t1);
    vec3 tmax_v = max(t0, t1);
    float tmin = max(max(tmin_v.x, tmin_v.y), tmin_v.z);
    float tmax = min(min(tmax_v.x, tmax_v.y), tmax_v.z);
    // 'carefully' designed so that if we start inside the box, we get a negative value but not inf.
    // but if we miss or start after the box, we return inf.
    return (tmin >= tmax || tmax<=0.) ? 1e9 : tmin;
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
        // if (grey > 0.5) {
        //     fg.xyz = vec3(1.) - fg.xyz;
        // }    
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
        o_color = pixel(v_uv);
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
            float believe_history = 0.75f;
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
        rendercol += bloomcol * 0.3;
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
        rendercol += bloomcol * 0.3;
        rendercol = max(vec3(0.), rendercol);
        float grey = dot(rendercol, vec3(0.2126 * 1., 0.7152 * 1., 0.0722 * 1.));

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
            // if (grey > 0.5) {
            //     beamcol = -beamcol * 2.;
            // }
            rendercol += max(vec3(0.),beamcol);
        }
        rendercol.rgb = sqrt(aces(rendercol.rgb)) * fade_render;

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

extern EditorState tabs[3];
EditorState tabs[3] = {
    {.fname = NULL, .editor_type = 0}, {.fname = NULL, .editor_type=1}, {.editor_type=2}};
EditorState *curE = tabs;
float ui_alpha = 0.f;
float ui_alpha_target = 0.f;
size_t textBytes = (size_t)(TMW * TMH * 4);
static float retina = 1.0f;

static float4 c_cam2world_old[4];
static float4 c_cam2world[4];
static float4 c_pos = {0.f, 2.f, 10.f, 1.f}; // pos;
static float4 c_lookat;
static float fov = 0.4f;
static float focal_distance = 5.f;
static float aperture = 0.01f;
void set_tab(EditorState *newE) {
    ui_alpha_target = (ui_alpha_target > 0.5 && curE == newE) ? 0.f : 1.f;
    curE = newE;
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
    json_print(&jp, "c_pos", c_pos);
    json_print(&jp, "c_lookat", c_lookat);
    json_print(&jp, "fov", fov);
    json_print(&jp, "focal_distance", focal_distance);
    json_end_object(&jp);
    json_start_object(&jp, "editor");
    json_print(&jp, "ui_alpha_target", ui_alpha_target);
    json_print(&jp, "cur_tab", (int)(curE - tabs));
    json_start_array(&jp, "tabs");
    for (int tab = 0; tab < 2; tab++) {
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

static void load_settings(int argc, char **argv, int *primon_idx, int *secmon_idx) {
    int cur_tab = 0;
    const char *settings_fname = "settings.json";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                printf(COLOR_YELLOW "Usage: " COLOR_CYAN "ginkgo" COLOR_RESET " [options] [filename]\n");
                printf("Options:\n");
                printf(COLOR_YELLOW "  --fullscreen, -f, -e index " COLOR_RESET
                                    " - run editor fullscreen on the specified monitor\n");
                printf(COLOR_YELLOW "  --settings, -s file.json " COLOR_RESET " - load settings from file\n");
                printf(COLOR_YELLOW "  --secmon, -m, -v index " COLOR_RESET " - use the specified monitor for visuals\n");
                printf(COLOR_YELLOW "  --help, -h " COLOR_RESET " - show this help message\n");
                printf("\nif filename is .glsl or .cpp it will be loaded as the first or second tab respectively\n");
                printf("if filename is .json it will be loaded as the settings file\n");
                _exit(0);
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
            tabs[0].fname = stbstring_from_span(argv[i], NULL, 0);
        else if (strstr(argv[i], ".cpp"))
            tabs[1].fname = stbstring_from_span(argv[i], NULL, 0);
        else if (strstr(argv[i], ".json"))
            settings_fname = argv[i];
    }
    sj_Reader r = read_json_file(settings_fname);
    for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
        if (iter_key_is(&outer, "camera")) {
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                if (iter_key_is(&inner, "c_pos")) {
                    c_pos = iter_val_as_float4(&inner, c_pos);
                } else if (iter_key_is(&inner, "c_lookat")) {
                    c_lookat = iter_val_as_float4(&inner, c_lookat);
                } else if (iter_key_is(&inner, "fov")) {
                    fov = iter_val_as_float(&inner, fov);
                } else if (iter_key_is(&inner, "focal_distance")) {
                    focal_distance = iter_val_as_float(&inner, focal_distance);
                }
            }
        } else if (iter_key_is(&outer, "editor")) {
            for (sj_iter_t inner = iter_start(outer.r, &outer.val); iter_next(&inner);) {
                if (iter_key_is(&inner, "ui_alpha_target")) {
                    ui_alpha_target = iter_val_as_float(&inner, ui_alpha_target);
                } else if (iter_key_is(&inner, "cur_tab")) {
                    cur_tab = iter_val_as_int(&inner, cur_tab);
                } else if (iter_key_is(&inner, "tabs")) {
                    int tabidx = 0;
                    for (sj_iter_t inner2 = iter_start(inner.r, &inner.val); tabidx < 2 && iter_next(&inner2); tabidx++) {
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
    if (tabs[0].fname == NULL)
        tabs[0].fname = stbstring_from_span("livesrc/blank.glsl", NULL, 0);
    if (tabs[1].fname == NULL)
        tabs[1].fname = stbstring_from_span("livesrc/audio.cpp", NULL, 0);
    if (cur_tab < 0 || cur_tab >= 2)
        cur_tab = 0;
    curE = &tabs[cur_tab];
    free_json(&r);
    ui_alpha = ui_alpha_target;
}

GLuint ui_pass = 0, user_pass = 0, bloom_pass = 0, taa_pass = 0, secmon_pass = 0;
GLuint vs = 0, fs_ui = 0, fs_bloom = 0, fs_taa = 0, fs_secmon = 0;
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
    if (E->editor_type!=0) {
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
    if (secmon_idx < 0 && count > 1) {
        // if more than 1 monitor, default to visuals on the secondary monitor
        GLFWmonitor *monitor_to_avoid = primon ? primon : actual_primon;
        for (int i = 0; i < count; i++)
            if (mons[i] != monitor_to_avoid) {
                secmon_idx = i;
                break;
            }
    }
    if (secmon_idx == primon_idx)
        secmon_idx = -1;
    if (secmon_idx >= 0 && secmon_idx < count)
        secmon = mons[secmon_idx];

    const GLFWvidmode *vm = primon ? glfwGetVideoMode(primon) : NULL;
    int ww = primon ? vm->width : 1920 / 2;
    int wh = primon ? vm->height : 1200 / 2;

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
    snprintf(winname, sizeof(winname), "ginkgo | %s | %s", tabs[0].fname, tabs[1].fname);
    GLFWwindow *win = glfwCreateWindow(ww, wh, winname, primon, NULL);
    if (!win)
        die("glfwCreateWindow failed");
    glfwGetWindowContentScale(win, &retina, NULL);
    // printf("retina: %f\n", retina);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(secmon ? 0 : 1); // let the secondary monitor determine the swap interval
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    if (secmon) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, false);
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
    last_mods = mods;
    EditorState *E = curE;
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;
    if (mods == 0) {
        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F3) {
            set_tab(&tabs[key - GLFW_KEY_F1]);
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
        if (key == GLFW_KEY_P) {
            if (!G->playing) {
                // also compile...
                parse_named_patterns_in_c_source(&tabs[1]);
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
            if (E->editor_type!=0 || try_to_compile_shader(E) != 0) {
                FILE *f = fopen("editor.tmp", "w");
                if (f) {
                    fwrite(E->str, 1, stbds_arrlen(E->str), f);
                    fclose(f);
                    if (rename("editor.tmp", E->fname) == 0) {
                        set_status_bar(C_OK, "saved");
                        init_remapping(E);
                        if (E->editor_type==1)
                            parse_named_patterns_in_c_source(E);
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
            if (E->editor_type==0)
                try_to_compile_shader(E);
            else if (E->editor_type==1)
                parse_named_patterns_in_c_source(E);
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
    if (curE->editor_type==2) return;
    curE->scroll_target_x -= xoffset * curE->font_width;
    curE->scroll_target_y -= yoffset * curE->font_height;
}

static void char_callback(GLFWwindow *win, unsigned int codepoint) {
    // printf("char: %d\n", codepoint);
    if (ui_alpha_target < 0.5)
        return;
    editor_key(win, curE, codepoint);
    curE->need_scroll_update = true;
}

static double click_mx, click_my, click_time;
static int click_count;

void draw_umap(EditorState *E, uint32_t *ptr) {
    int tmw = fbw / E->font_width;
    int tmh = fbh / E->font_height;
    if (tmw > 512)
        tmw = 512;
    if (tmh > 256 - 8)
        tmh = 256 - 8;
    if (!E->embeddings) {
        FILE *f = fopen("embeddings.bin", "rb");
        sj_Reader r = read_json_file("webcache/umap_sounds.json");
        for (sj_iter_t outer = iter_start(&r, NULL); iter_next(&outer);) {
            sample_embedding_t t = {.key = iter_key_as_stbstring(&outer)};
            sj_iter_t inner = iter_start(&r, &outer.val);
            iter_next(&inner);
            t.x = iter_val_as_float(&inner, t.x);
            iter_next(&inner);
            t.y = iter_val_as_float(&inner, t.y);
            iter_next(&inner);
            int r=128, g=128, b=128;
            r = clamp((int)(iter_val_as_float(&inner, r) * 255.f), 0, 255);
            iter_next(&inner);
            g = clamp((int)(iter_val_as_float(&inner, g) * 255.f), 0, 255);
            iter_next(&inner);
            b = clamp((int)(iter_val_as_float(&inner, b) * 255.f), 0, 255);
            t.col = (r << 16) | (g << 8) | b;
            iter_next(&inner);
            t.mindist = iter_val_as_float(&inner, t.mindist);
            iter_next(&inner);
            E->old_closest_idx = -1;
            int i;
            t.wave_idx = -1;
            t.sound_idx = -1;
            t.sound_number = 0;
            stbds_shputs(E->embeddings, t);
        }
        printf("loaded %d embeddings\n", (int)stbds_shlen(E->embeddings));
        // find mapping from sounds back to embeddings
        int ns = stbds_shlen(G->sounds);
        for (int i = 0; i < ns; ++i) {
            Sound *s = G->sounds[i].value;
            for (int j = 0; j < stbds_arrlen(s->wave_indices); ++j) {
                int wi = s->wave_indices[j];
                wave_t *w = &G->waves[wi];
                char name[1024];
                void sanitize_url_for_local_filesystem(const char *url, char *out, size_t cap);
                sanitize_url_for_local_filesystem(w->key, name, sizeof name);
                int embed_i = stbds_shgeti(E->embeddings, name);
                if (embed_i != -1) {
                    E->embeddings[embed_i].sound_idx = i;
                    E->embeddings[embed_i].sound_number = j;
                    E->embeddings[embed_i].wave_idx = wi;
                }
            }
        }
        E->zoom = 0.f;
        E->centerx = fbw / 2.f;
        E->centery = fbh / 2.f;
        free_json(&r);
    }
    //float extra_size = max(maxx - minx, maxy - miny) / sqrtf(1.f + num_after_filtering) * 0.125f;
    
    if (G->mscrolly!=0.f) {
        float mx_e = (G->mx - E->centerx) / E->zoom;
        float my_e = (G->my - E->centery) / E->zoom;
        E->zoom *= exp2f(G->mscrolly * -0.02f);
        E->zoom = clamp(E->zoom, 0.01f, 100.f);
        E->centerx = G->mx - mx_e * E->zoom;
        E->centery = G->my - my_e * E->zoom;
        G->mscrolly = 0.f;
    }
    int n = stbds_shlen(E->embeddings);
    int closest_idx = 0;
    float closest_x = G->mx;
    float closest_y = G->my;
    float closest_d2 = 1e10;
    auto draw_point = [&](int i, float extra_size, bool matched) -> float2 {
        sample_embedding_t *e = &E->embeddings[i];
        int col = matched ? e->col | 0x60000000 : 0x10202020;
        float x = e->x * E->zoom_sm + E->centerx_sm;
        float y = e->y * E->zoom_sm + E->centery_sm;
        add_line(x, y, x, y, col, /*point_size*/ e->mindist * E->zoom_sm + extra_size);
        return float2{x, y};
    };
    int num_after_filtering = 0;
    int filtlen = find_end_of_line(E, 0);
    int new_filter_hash = fnv1_hash(E->str, E->str + filtlen);
    bool autozoom = (new_filter_hash != E->filter_hash) || E->zoom<=0.f;
    E->filter_hash = new_filter_hash;
    float minx=1e10, miny=1e10;
    float maxx=-1e10, maxy=-1e10;
    int parsed_number = 0;
    const char *line = NULL;
    const char *colon = NULL;
    float fromt = 0.f;
    float tot = 1.f;
    if (E->cursor_y>0) {
        // parse the line
        int start_idx = find_start_of_line(E, E->cursor_idx);
        int end_idx = find_end_of_line(E, E->cursor_idx);
        line = temp_cstring_from_span(E->str + start_idx, E->str + end_idx);
        colon=strchr(line,':');
        if (!colon) colon=line+strlen(line); else parsed_number=atoi(colon+1);
        const char *s=colon;
        while (*s && !isspace(*s)) s++;
        const char *from = strstr(s," from ");
        const char *to = strstr(s," to ");
        if (from) fromt=clamp(atof(from+5), 0.f, 1.f);
        if (to) { tot=clamp(atof(to+3), 0.f, 1.f); if (!tot) tot=1.f; }
        if (fromt>=tot) {
            float t=fromt; fromt=tot; tot=t;
        }
    }
    if (E->drag_type != 0 && G->mb == 1) {
        if (E->drag_type == 3) {
            E->centerx += G->mx - click_mx;
            E->centery += G->my - click_my;
            click_mx = G->mx;
            click_my = G->my;
        } else {
            if (E->drag_type == 1) {
                fromt += (G->mx - click_mx) / (fbw-96.f);
            } else if (E->drag_type == 2) {
                tot += (G->mx - click_mx) / (fbw-96.f);
            } else if (E->drag_type == 4) {
                fromt += (G->mx - click_mx) / (fbw-96.f);
                tot += (G->mx - click_mx) / (fbw-96.f);
            }
            click_mx = G->mx;
            fromt = saturate(fromt);
            tot = saturate(tot);
            fromt = clamp(fromt, 0.f, tot);
            tot = clamp(tot, fromt, 1.f);
        if (E->cursor_y > 0) {
                int start_idx = find_start_of_line(E, E->cursor_idx);
                int end_idx = find_end_of_line(E, E->cursor_idx);
                char buf[1024];
                int n = snprintf(buf, sizeof(buf), "%s:%d from %0.5g to %0.5g", G->sounds[E->closest_sound_idx].value->name, E->closest_sound_number, fromt, tot);
                stbds_arrdeln(E->str, start_idx, end_idx - start_idx);
                stbds_arrinsn(E->str, start_idx, n);
                memcpy(E->str + start_idx, buf, n);
                E->cursor_idx = start_idx + n;
                E->select_idx = E->cursor_idx;
            }
        }
    }

    ////////////// draw it!

    float2 matched_p = float2{0.f, 0.f};
    for (int i = 0; i < n; ++i) {
        sample_embedding_t *e = &E->embeddings[i];
        if (e->sound_idx==-1) continue;
        const char *soundname = G->sounds[e->sound_idx].value->name;
        char soundname_with_colon[1024];
        snprintf(soundname_with_colon, sizeof soundname_with_colon, "%s:%d", soundname, e->sound_number);
        bool matched = true;
        if (E->cursor_y>0) {
            matched = strlen(soundname) == colon-line && strncasecmp(soundname, line, colon-line)==0 && parsed_number==e->sound_number;
        }
        else if (filtlen) {
            matched = false;
            // find the words separated by whitespace in E->str
            const char *ws = E->str;
            const char *end = E->str + filtlen;
            while (ws<end) {
                while (ws < end && isspace(*ws)) ws++;
                const char *we = ws;
                while (we < end && !isspace(*we)) we++;
                if (we>ws) {
                    for (const char *s=soundname_with_colon; *s; s++) {
                        if (strncasecmp(s, ws, we-ws)==0) { matched = true; break; }
                    }
                }
                ws=we;
            }
        }
        bool matched_or_shift = matched || ((last_mods & GLFW_MOD_SHIFT) && E->cursor_y==0);
        float2 p = draw_point(i, 1.f, matched_or_shift);
        if (matched) {
            minx = min(minx, e->x);
            miny = min(miny, e->y);
            maxx = max(maxx, e->x);
            maxy = max(maxy, e->y);
            matched_p = p;
            num_after_filtering++;
        }
        if (matched_or_shift) {
            float d2 = square(G->mx - p.x) + square(G->my - p.y);
            if (d2 < closest_d2) {
                closest_d2 = d2;
                closest_idx = i;
                closest_x = p.x;
                closest_y = p.y;
            }
        }        
    }
    draw_point(closest_idx, 30.f, true);
    if (G->mb == 0 && (E->old_closest_idx != closest_idx || fromt!=G->preview_fromt || tot!=G->preview_tot)) {
        if (G->preview_wave_fade > 0.01f) {
            G->preview_wave_fade *= 0.999f; // start the fade
        } else {
            int wi = E->embeddings[closest_idx].wave_idx;
            wave_t *w = &G->waves[wi];
            if (wi!=-1)
                request_wave_load(w);
            if (w->num_frames) {
                E->old_closest_idx = closest_idx;
                E->closest_sound_idx = E->embeddings[closest_idx].sound_idx;
                E->closest_sound_number = E->embeddings[closest_idx].sound_number;
                G->preview_fromt = fromt;
                G->preview_tot = tot;
                G->preview_wave_t = 0.;
                G->preview_wave_idx_plus_one = wi + 1;
                G->preview_wave_fade = 1.f;
            }
        }
    }
    if (E->closest_sound_idx != -1) {
        Sound *s = G->sounds[E->closest_sound_idx].value;
        const char *name = s->name;
        print_to_screen(ptr, closest_x / E->font_width + 2.f, closest_y / E->font_height - 0.5f, C_SELECTION, false, "%s:%d", name,
                        E->closest_sound_number);
        if (closest_idx!=-1) {
            sample_embedding_t *e = &E->embeddings[closest_idx];
            wave_t *w = &G->waves[e->wave_idx];
            int smp0 = 0;
            float startx = 48.f + fromt * (fbw-96.f);
            float endx = 48.f + tot * (fbw-96.f);
            if (w->num_frames) {
                float playpos_frac = fromt + (G->preview_wave_t * w->sample_rate / w->num_frames);
                if (playpos_frac >= fromt && playpos_frac < tot) {
                    float playx = 48.f + playpos_frac * (fbw-96.f);
                    add_line(playx, fbh-256.f, playx, fbh, 0xffffffff, 4.f);
                }
            }
            add_line(startx, fbh-256.f, startx, fbh, 0xffeeeeee, 3.f);
            add_line(endx, fbh-256.f, endx, fbh, 0xffeeeeee, 3.f);
            for (int x=48;x<fbw-48.f;++x) {
                float f = (x - 48.f) / (fbw-96.f);
                int smp1 = ((int)(f * w->num_frames)) * w->channels;
                float mn=1e10, mx=-1e10;
                for (int s=smp0;s<smp1;++s) {
                    float v = w->frames[s];
                    mn = min(mn, v);
                    mx = max(mx, v);
                }
                float ymid = fbh-128.f;
                add_line(x, ymid + mn * 128.f, x, ymid + mx * 128.f, (f>=fromt && f<tot) ? e->col : 0x40404040, 2.f);
                smp0 = smp1;
            }
        }
    }
    if (num_after_filtering==1 && (matched_p.x < 32.f || matched_p.x > fbw-32.f || matched_p.y < 32.f || matched_p.y > fbh-32.f)) {
        autozoom = true;
    }
    if (autozoom) {
        if (num_after_filtering>1) {
            E->zoom = min(fbw*0.75f / (maxx - minx + 10.f), fbh*0.9f / (maxy - miny + 10.f));
        }
        E->zoom = clamp(E->zoom, 0.01f, 100.f);
        E->centerx = fbw / 2.f - (maxx + minx) / 2.f * E->zoom;
        E->centery = fbh / 2.f - (maxy + miny) / 2.f * E->zoom;
    }
    E->zoom = clamp(E->zoom, 0.01f, 100.f);
    E->zoom_sm += (E->zoom - E->zoom_sm) * 0.1f;
    E->centerx_sm += (E->centerx - E->centerx_sm) * 0.1f;
    E->centery_sm += (E->centery - E->centery_sm) * 0.1f;
}

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
        editor_click(win, curE, G, mx, my, -1, click_count);
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
        editor_click(win, E, G, mx, my, 1, 0);
        E->need_scroll_update = true;
    }

    int tmw = fbw / E->font_width;
    int tmh = fbh / E->font_height;
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

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbos[pbo_index]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, textBytes, NULL, GL_STREAM_DRAW);
    uint32_t *ptr = (uint32_t *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, textBytes,
                                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (ptr) {
        memset(ptr, 0, textBytes);
        E->intscroll_x = (int)(E->scroll_x / E->font_width);
        E->intscroll_y = (int)(E->scroll_y / E->font_height);
        code_color(E, ptr);

        if (E == &tabs[2]) {
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
            fft_buf[2][i] = (pr.l + pr.r) * fft_window[i];
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

static void unbind_textures_from_slots(int num_slots) {
    for (int i = 0; i < num_slots; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void update_camera(GLFWwindow *win) {
    memcpy(c_cam2world_old, c_cam2world, sizeof(c_cam2world));
    if (ui_alpha_target < 0.5) {
        float cam_mx = 0.5f - G->mx / fbw - 0.25f;
        float cam_my = 0.5f - G->my / fbh;
        float theta = cam_mx * TAU;
        float phi = cam_my * PI;
        float rc = focal_distance * cosf(phi);
        c_lookat = c_pos + float4{cosf(theta) * rc, sinf(phi) * focal_distance, sinf(theta) * rc, 0.f}; // pos
    }

    float4 c_up = {0.f, 1.f, 0.f, 0.f}; // up
    float4 c_fwd = normalize(c_lookat - c_pos);
    float4 c_right = normalize(cross(c_up, c_fwd));
    c_up = normalize(cross(c_fwd, c_right));
    if (ui_alpha_target < 0.5) {
        const float speed = 0.1f;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) {
            c_pos += c_fwd * speed;
        }
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) {
            c_pos -= c_fwd * speed;
        }
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) {
            c_pos -= c_right * speed;
        }
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) {
            c_pos += c_right * speed;
        }
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS) {
            c_pos += c_up * speed;
        }
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS) {
            c_pos -= c_up * speed;
        }
    }
    c_cam2world[0] = c_right;
    c_cam2world[1] = c_up;
    c_cam2world[2] = c_fwd;
    c_cam2world[3] = c_pos;
    if (!dot(c_cam2world_old[0], c_cam2world_old[0])) {
        memcpy(c_cam2world_old, c_cam2world, sizeof(c_cam2world));
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
    glUniformMatrix4fv(glGetUniformLocation(taa_pass, "c_cam2world"), 1, GL_FALSE, (float *)c_cam2world);
    glUniformMatrix4fv(glGetUniformLocation(taa_pass, "c_cam2world_old"), 1, GL_FALSE, (float *)c_cam2world_old);
    glUniform1f(glGetUniformLocation(taa_pass, "fov"), fov);
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

GLuint load_texture(const char *fname, int filter_mode = GL_LINEAR, int wrap_mode = GL_CLAMP_TO_EDGE) {
    int w, h, n;
    void *img = NULL;
    int hdr = stbi_is_hdr(fname);
    if (hdr) {

        img = stbi_loadf(fname, &w, &h, &n, 4);
        if (!img)
            die("failed to load hdr texture", fname);
        float *fimg = (float *)img;
#define MAX_FLOAT16 65504.f
        for (int i = 0; i < w * h * 4; i++) {
            if (fimg[i] >= MAX_FLOAT16 || isinf(fimg[i]))
                fimg[i] = MAX_FLOAT16;
        }
    } else {
        img = stbi_load(fname, &w, &h, &n, 4);
        if (!img)
            die("failed to load texture", fname);
    }
    GLuint tex = gl_create_texture(filter_mode, wrap_mode);
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
    return tex;
}

void init_dynamic_buffer(GLuint vao[2], GLuint vbo[2], int attrib_count, const uint32_t *attrib_sizes, const uint32_t *attrib_types,
                         const uint32_t *attrib_offsets, uint32_t max_elements, uint32_t stride) {
    glGenVertexArrays(2, vao);
    glGenBuffers(2, vbo);
    for (int i = 0; i < 2; i++) {
        glBindVertexArray(vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
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

struct sky_t {
    char *key;
    const char *value;
    int texture;
} *skies = NULL;

int get_sky(const char *key) {
    if (!skies) {
        char *json = load_file("assets/skies.json");
        sj_Reader r = sj_reader(json, stbds_arrlen(json));
        sj_Value outer_obj = sj_read(&r);
        sj_Value key, val;
        while (sj_iter_object(&r, outer_obj, &key, &val)) {
            if (val.type == SJ_STRING) {
                const char *k = stbstring_from_span(key.start, key.end, 0);
                const char *v = stbstring_from_span(val.start, val.end, 0);
                stbds_shput(skies, k, v);
            }
        }
        printf("loaded list of %d skies\n", (int)stbds_shlen(skies));
    }
    sky_t *sky = stbds_shgetp(skies, key);
    if (sky->texture)
        return sky->texture;
    const char *url = sky->value;
    if (!url)
        return 0;
    const char *fname = fetch_to_cache(url, 1);
    if (!fname)
        return 0;
    return sky->texture = load_texture(fname);
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
        glUniform2f(glGetUniformLocation(shader, "uARadjust"), scale, 1.f);
    } else { // black bars at top and bottom
        glUniform2f(glGetUniformLocation(shader, "uARadjust"), 1.f, 1.f / scale);
    }
    check_gl("set_aradjust");
}

int main(int argc, char **argv) {
    printf(COLOR_CYAN "ginkgo" COLOR_RESET " - " __DATE__ " " __TIME__ "\n");
    void install_crash_handler(void);
    install_crash_handler();
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
    load_settings(argc, argv, &primon_idx, &secmon_idx);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    init_sampler();

    void test_minipat(void);
    test_minipat();
    // return 0;

    GLFWwindow *win = gl_init(primon_idx, secmon_idx);

    size_t n = strlen(kFS_user_prefix) + strlen(kFS_ui_suffix) + 1;
    char *fs_ui_str = (char *)malloc(n);
    snprintf(fs_ui_str, n, "%s%s", kFS_user_prefix, kFS_ui_suffix);
    vs = compile_shader(NULL, GL_VERTEX_SHADER, kVS);
    fs_ui = compile_shader(NULL, GL_FRAGMENT_SHADER, fs_ui_str);
    free(fs_ui_str);
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

    load_file_into_editor(&tabs[0]);
    load_file_into_editor(&tabs[1]);
    try_to_compile_shader(&tabs[0]);
    parse_named_patterns_in_c_source(&tabs[1]);

    texFont = load_texture("assets/font_sdf.png");

    // GLuint paper_diff = load_texture("assets/textures/paper-rough-1-DIFFUSE.png", GL_LINEAR, GL_REPEAT);
    // GLuint paper_disp = load_texture("assets/textures/paper-rough-1-DISP.png", GL_LINEAR, GL_REPEAT);
    // GLuint paper_norm = load_texture("assets/textures/paper-rough-1-NORM.png", GL_LINEAR, GL_REPEAT);

    texText = gl_create_texture(GL_NEAREST, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, TMW, TMH, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, NULL);
    check_gl("alloc text tex");
    glBindTexture(GL_TEXTURE_2D, 0);

#define NUM_BLOOM_MIPS 4
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
    static const uint32_t attrib_sizes[4] = {2, 2, 1, 4};
    static const uint32_t attrib_types[4] = {GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_UNSIGNED_BYTE};
    static const uint32_t attrib_offsets[4] = {offsetof(line_t, p0x), offsetof(line_t, p1x), offsetof(line_t, width),
                                               offsetof(line_t, col)};
    init_dynamic_buffer(fatvao, fatvbo, 4, attrib_sizes, attrib_types, attrib_offsets, MAX_LINES, sizeof(line_t));
    check_gl("init fat line buffer post");

    // sphere init
    GLuint spheretbotex;
    glGenTextures(1, &spheretbotex);

    GLuint spheretbos[2];
    glGenBuffers(2, spheretbos);
    check_gl("init sphere buffer post");

    double t0 = glfwGetTime();
    glfwSetKeyCallback(win, key_callback);
    glfwSetCharCallback(win, char_callback);
    glfwSetScrollCallback(win, scroll_callback);
    glfwSetMouseButtonCallback(win, mouse_button_callback);

    ma_device dev;
    init_audio_midi(&dev);

    double start_time = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        glfwGetFramebufferSize(win, &fbw, &fbh);

        double iTime = glfwGetTime() - t0;
        static uint32_t iFrame = 0;

        glDisable(GL_DEPTH_TEST);
        editor_update(curE, win);
        const char *s = tabs[0].str;
        const char *e = tabs[0].str + stbds_arrlen(tabs[0].str);
        stbds_arrpush(tabs[0].str, 0);
        const char *sky_name = strstr(s, "// sky ");
        const char *want_taa_str = strstr(s, "// taa");
        if (want_taa_str && want_taa_str[6] == ' ' && want_taa_str[7] == '0')
            want_taa_str = NULL;
        bool want_taa = want_taa_str != NULL;
        const char *aperture_str = strstr(s, "// aperture ");
        if (aperture_str)
            aperture = atof_default(aperture_str + 11, aperture);
        const char *focal_distance_str = strstr(s, "// focal_distance ");
        if (focal_distance_str)
            focal_distance = atof_default(focal_distance_str + 17, focal_distance);
        const char *fov_str = strstr(s, "// fov ");
        if (fov_str)
            fov = atof_default(fov_str + 7, fov);
        stbds_arrpop(tabs[0].str);
        static uint32_t skytex = 0;
        if (sky_name) {
            s = sky_name + 6;
            while (s < e && *s != '\n' && isspace(*s))
                ++s;
            const char *spans = s;
            while (s < e && *s != '\n' && !isspace(*s))
                ++s;
            sky_name = temp_cstring_from_span(spans, s);
            if (sky_name) {
                int newtex = get_sky(sky_name);
                if (newtex)
                    skytex = newtex;
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
            glUniform2i(glGetUniformLocation(user_pass, "uScreenPx"), RESW, RESH);
            glUniform1f(glGetUniformLocation(user_pass, "iTime"), (float)iTime);
            glUniform1ui(glGetUniformLocation(user_pass, "iFrame"), iFrame);
            glUniform2i(glGetUniformLocation(user_pass, "uScreenPx"), fbw, fbh);
            glUniform2i(glGetUniformLocation(user_pass, "uFontPx"), curE->font_width, curE->font_height);
            glUniform1i(glGetUniformLocation(user_pass, "status_bar_size"), status_bar_color ? 1 : 0);
            glUniform2f(glGetUniformLocation(user_pass, "scroll"), curE->scroll_x - curE->intscroll_x * curE->font_width,
                        curE->scroll_y - curE->intscroll_y * curE->font_height);
            glUniform1f(glGetUniformLocation(user_pass, "ui_alpha"), ui_alpha);

            bind_texture_to_slot(user_pass, 1, "uFont", texFont, GL_LINEAR);
            bind_texture_to_slot(user_pass, 2, "uText", texText, GL_NEAREST);
            bind_texture_to_slot(user_pass, 0, "uFP", texFPRT[(iFrame + 1) % 2], GL_NEAREST);
            bind_texture_to_slot(user_pass, 3, "uSky", skytex, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 4, "uPaperDiff", paper_diff, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 5, "uPaperDisp", paper_disp, GL_LINEAR);
            // bind_texture_to_slot(user_pass, 6, "uPaperNorm", paper_norm, GL_LINEAR);
            glUniform1i(glGetUniformLocation(user_pass, "uSpheres"), 7);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_BUFFER, spheretbotex);

            glUniformMatrix4fv(glGetUniformLocation(user_pass, "c_cam2world"), 1, GL_FALSE, (float *)c_cam2world);
            glUniformMatrix4fv(glGetUniformLocation(user_pass, "c_cam2world_old"), 1, GL_FALSE, (float *)c_cam2world_old);
            glUniform4f(glGetUniformLocation(user_pass, "c_lookat"), c_lookat.x, c_lookat.y, c_lookat.z, focal_distance);
            glUniform1f(glGetUniformLocation(user_pass, "fov"), fov);
            glUniform1f(glGetUniformLocation(user_pass, "focal_distance"), focal_distance);
            glUniform1f(glGetUniformLocation(user_pass, "aperture"), aperture);

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
        set_aradjust(ui_pass, fbw, fbh);

        glUniform2i(glGetUniformLocation(ui_pass, "uScreenPx"), fbw, fbh);
        glUniform2i(glGetUniformLocation(ui_pass, "uFontPx"), curE->font_width, curE->font_height);
        glUniform1i(glGetUniformLocation(ui_pass, "status_bar_size"), status_bar_color ? 1 : 0);
        glUniform1f(glGetUniformLocation(ui_pass, "iTime"), (float)iTime);
        glUniform2f(glGetUniformLocation(ui_pass, "scroll"), curE->scroll_x - curE->intscroll_x * curE->font_width,
                    curE->scroll_y - curE->intscroll_y * curE->font_height);
        glUniform1f(glGetUniformLocation(ui_pass, "ui_alpha"), ui_alpha);
        // if there's a second monitor, we can afford to fade the render a bit more
        // to make the code more readable.
        float fade_render = lerp(1.f, winFS ? 0.5f : 0.8f, ui_alpha);
        glUniform1f(glGetUniformLocation(ui_pass, "fade_render"), fade_render);

        float f_cursor_x = (curE->cursor_x - curE->intscroll_x) * curE->font_width;
        float f_cursor_y = (curE->cursor_y - curE->intscroll_y) * curE->font_height;
        glUniform4f(glGetUniformLocation(ui_pass, "cursor"), f_cursor_x, f_cursor_y, curE->prev_cursor_x, curE->prev_cursor_y);
        curE->prev_cursor_x += (f_cursor_x - curE->prev_cursor_x) * CURSOR_SMOOTH_FACTOR;
        curE->prev_cursor_y += (f_cursor_y - curE->prev_cursor_y) * CURSOR_SMOOTH_FACTOR;

        draw_fullscreen_pass(0, fbw, fbh, vao);
        unbind_textures_from_slots(4);
        //////////////////////////////
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
            int alpha = (int)clamp(len, 0.f, 255.f);
            // red is in the lsb. premultiplied alpha so alpha=0 == additive
            uint32_t col = (alpha << 24) | ((alpha >> 0) << 16) | ((alpha >> 0) << 8) | (alpha >> 0);
            if (i == 0)
                col = 0;
            add_line(p0x, p0y, p1x, p1y, col, 17.f - i);
        }

        static const uint32_t cc_cols[] = {
            0x3344ee,
            0x3344ee,
            0x4477ee,
            0x4477ee,
            0x33ccff,
            0x33ccff,
            0xffffee,
            0xffffee,
        };
        for (int i =0; i < 8; ++i) {
            float x = fbw+(i-7.5f)*30.f;
            float y = fbh - curE->font_height;
            float y2 = y - G->midi_cc[i+0x10];
            add_line(x, y-128.f, x, y, 0x3f000000 | ((cc_cols[i]>>2)&0x3f3f3f), -20.f); // negative width is square cap
            add_line(x, y, x, y2, 0x3f000000 | (cc_cols[i]), -20.f);
        }

        draw_logo(iTime - start_time);
        // test_svf_gain();

        // fat line draw
        if (line_count > 0) {
            // line_t lines[1] = {{300.f,100.f,G->mx,G->my,0xffff00ff,54.f}};
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

        // poll for changes in the audio file
        static time_t last = 0;
        struct stat st;
        if (stat(tabs[1].fname, &st) == 0 && st.st_mtime != last) {
            last = st.st_mtime;
            try_to_compile_audio(tabs[1].fname, &tabs[1].last_compile_log);
            parse_error_log(&tabs[1]);
        }

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
