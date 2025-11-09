

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

static inline float sqr(const cf& z) { return (z.re*z.re + z.im*z.im); }
static cf expj(float w) { return {cosf(w), sinf(w)}; }
typedef struct svf_gain_output_t {
    cf lp, bp, hp;
} svf_gain_output_t;

static inline svf_gain_output_t calculate_svf_gain_2pole(float f, float g, float R) {
    svf_gain_output_t rv{};
    const float w = 2.0f * PI * f / SAMPLE_RATE;
    const cf z = expj(w), z2 = z*z;

    const float g2 = g*g;
    const float a = 1.0f + g*R + g2;
    const float b = 2.0f*g2 - 2.0f;
    const float c = 1.0f - g*R + g2;
    const cf den = a*z2 + b*z + c;
    if (sqr(den) < 1e-10f) return rv;

    const cf zp1 = z + cf(1,0);
    const cf zm1 = z - cf(1,0);
    const cf h_lp = (g2 * zp1*zp1) / den;
    const cf h_bp = (g * (z2 - cf(1,0))) / den;
    const cf h_hp = (zm1*zm1) / den;
    return {h_lp, h_bp, h_hp};
}

static inline svf_gain_output_t calculate_svf_gain_1pole(float f, float g){
    svf_gain_output_t rv = {};
    const float w = 2.0f * PI * f / SAMPLE_RATE;
    const cf z = expj(w);

    const float gp1 = (1.f + g), gm1 = (1.f - g);
    const float r   = gm1 / gp1;                  // pole on real axis
    const cf den    = z - cf(r,0);

    const cf h_lp = (g/gp1) * (z + cf(1,0)) / den;
    const cf h_hp = (1.f/gp1) * (z - cf(1,0)) / den;
    const cf h_bp = (g/gp1) * (z - cf(1,0)) / den;
    return {h_lp, h_bp, h_hp};
}


void add_line(float p0x, float p0y, float p1x, float p1y, uint32_t col, float width);


void test_svf_gain(void) {
    float fhpf = 100.f;
    float fmid = 880.f;
    float qmid = 1.;
    float gainmid = 0.1f;
    float flpf = 5000.f;
    float ghpf = svf_g(fhpf);
    float Rhpf1 = 1.f/QBUTTER_24A;
    float Rhpf2 = 1.f/QBUTTER_24B;
    float glpf = svf_g(flpf);
    float Rlpf = 1.f/QBUTTER;
    float gmid = svf_g(fmid);
    float Rmid = 1.f/(qmid);

    float prevy=0.f;
    float prevx=0.f;
    float prevy2=0.f;
    int dx=16;
    float xhp = log2f(fhpf/48.f)*200.f;
    float xmid = log2f(fmid/48.f)*200.f;
    float xlp = log2f(flpf/48.f)*200.f;
    add_line(xhp, 200.f, xhp, 1500.f, 0xffff0000, 4.f);
    add_line(xmid, 200.f, xmid, 1500.f, 0xffff0000, 4.f);
    add_line(xlp, 200.f, xlp, 1500.f, 0xffff0000, 4.f);
    for (int bin = 4; bin < FFT_SIZE/2; bin++) {
        float freq = bin * (float)SAMPLE_RATE_OUTPUT / (float)FFT_SIZE;
        float x = log2f(freq/48.f)*200.f;
        float signal_mag_db = probe_db_smooth[bin];
        svf_gain_output_t gain1 = calculate_svf_gain_2pole(freq, ghpf, Rhpf1);
        svf_gain_output_t gain2 = calculate_svf_gain_2pole(freq, ghpf, Rhpf2);
        svf_gain_output_t gain3 = calculate_svf_gain_2pole(freq, glpf, Rlpf);
        svf_gain_output_t gain4 = calculate_svf_gain_2pole(freq, gmid, Rmid);
        float gainsq = sqr(gain1.hp * gain2.hp * gain3.lp);
        gainsq = sqr(gain4.bp * Rmid * (gainmid-1.f) + cf{1.f,0.f});
        float db = squared2db(gainsq);
        
        if (db<-100.f) db=-100.f;
        if (signal_mag_db<-100.f) signal_mag_db=-100.f;
        float y = db * -10.f + 500.f;
        float y2 = signal_mag_db * -10.f + 500.f;
        if (x) {
            add_line(x, y, prevx, prevy, 0xffffffff, 4.f);
            add_line(x, y2, prevx, prevy2, 0xff0000ff, 4.f);
        }
        prevy = y;
        prevy2 = y2;
        prevx = x;
    }
}
