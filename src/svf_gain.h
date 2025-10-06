

struct cf {
    float re, im;
    cf(float r=0, float i=0): re(r), im(i) {}
    cf operator+(const cf& o) const { return {re+o.re, im+o.im}; }
    cf operator-(const cf& o) const { return {re-o.re, im-o.im}; }
    cf operator*(const cf& o) const { return {re*o.re - im*o.im, re*o.im + im*o.re}; }
    cf operator*(float s) const { return {re*s, im*s}; }
    friend cf operator*(float s, const cf& z) { return z*s; }
    cf operator/(const cf& o) const {
        float d = o.re*o.re + o.im*o.im;
        return {(re*o.re + im*o.im)/d, (im*o.re - re*o.im)/d};
    }
};

static inline float abs(const cf& z) { return sqrtf(z.re*z.re + z.im*z.im); }
static cf expj(float w) { return {cosf(w), sinf(w)}; }
typedef struct svf_gain_output_t {
    cf lp, bp, hp;
} svf_gain_output_t;

static inline svf_gain_output_t calculate_svf_gain(float f_norm, float g, float R, float /*peak_gain_db*/) {
    svf_gain_output_t rv{};
    const float w = 2.0f * PI * f_norm;
    const cf z = expj(w), z2 = z*z;

    const float g2 = g*g;
    const float a = 1.0f + g*R + g2;
    const float b = 2.0f*g2 - 2.0f;
    const float c = 1.0f - g*R + g2;
    const cf den = a*z2 + b*z + c;
    if (abs(den) < 1e-10f) return rv;

    const cf zp1 = z + cf(1,0);
    const cf zm1 = z - cf(1,0);
    const cf h_lp = (g2 * zp1*zp1) / den;
    const cf h_bp = (g * (z2 - cf(1,0))) / den;
    const cf h_hp = (zm1*zm1) / den;
    return {h_lp, h_bp, h_hp};
}


        /* visualize svf gain 
        float prevy=0.f;
        int dx=16;
        for (int x=0;x<1024;x+=dx) {
            float f_norm = (48.f * powf(500.f, x/1024.f)) / SAMPLE_RATE;
            float g = 440.f * 2.f / SAMPLE_RATE;
            float Q = 400.f;
            float R = 1.f / Q;
            float peak_gain_db = 0.f;
            svf_gain_output_t gain = calculate_svf_gain(f_norm, g, R, peak_gain_db);
            float lp = abs(gain.lp);
            float bp = abs(gain.bp);
            float hp = abs(gain.hp);
            float linear_gain = 2.f;
            float peakp = abs(gain.lp + gain.hp + linear_gain * gain.bp);
            float db = lin2db(lp);
            float y = db * -5.f + 500.f;
            if (x)
                add_line(x, y, x-dx, prevy, 0xffffffff, 4.f);
            prevy = y;
        }
            */

