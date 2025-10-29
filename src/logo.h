#include "logo2.h"


static inline float estimate_segment_length(const segment_t* s) {
    const float2 p0=s->p0, p1=s->p1, p2=s->p2, p3=s->p3;
    const float chord = length(p3 - p0);
    const float ctrl  = length(p1 - p0) + length(p2 - p1) + length(p3 - p2);
    return 0.5f * (ctrl + chord); // crude + fast
}

static inline float2 segment_point(const segment_t* s, float t) {
    const float2 p0=s->p0, p1=s->p1, p2=s->p2, p3=s->p3;
    const float u = 1.f - t;
    // de Casteljau (branchless-ish)
    const float2 a0 = p0*u + p1*t;
    const float2 a1 = p1*u + p2*t;
    const float2 a2 = p2*u + p3*t;
    const float2 b0 = a0*u + a1*t;
    const float2 b1 = a1*u + a2*t;
    return b0*u + b1*t;
}

void draw_logo(float time) {
    if (time > 8.f || time < 0.f) return;
    int numsegs = sizeof(segments) / sizeof(segments[0]);
    float offset = 1000.f;
    for (int i = 0; i < numsegs; i++) {
        const segment_t* s = &segments[i];
        float len = s->length;
        if (i>0) {
            float jumplen = length(segments[i-1].p3 - s->p0);
            offset -= jumplen * 0.5f;
        }

        float mint = time * 1700.f - offset - 8000.f;
        float maxt = time * 1300.f - offset;
        //mint = -10000.f;
        offset += len;
        if (maxt<=0.f) continue;
        if (mint>=len) continue;
        float width_scale = clamp(min(maxt,len-mint) * 0.01f, 0.f, 1.f);
        if (maxt>=len) maxt=len;
        if (mint<0.f) mint=0.f;
        int n = (int)((maxt - mint) / 5.f) + 4;
        float2 prev_p = segment_point(s, mint / len);
        for (int j = 1; j <= n; j++) {
            float t = ((float)j / (float)n * (maxt-mint) + mint) / len;
            float2 p = segment_point(s, t);
            float scale = 2.3f;
            add_line(prev_p.x * scale, prev_p.y * scale, p.x * scale, p.y * scale, s->color | 0xff000000u, s->stroke_width * scale * width_scale);
            prev_p = p;
        }
    }     
}
