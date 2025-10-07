

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

static inline svf_gain_output_t calculate_svf_gain_2pole(float f, float g, float R) {
    svf_gain_output_t rv{};
    const float w = 2.0f * PI * f / SAMPLE_RATE;
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
    static PFFFT_Setup *fft_setup = NULL;
    static float *fft_work = NULL;
    static float *fft_buf = NULL;
    static float *fft_mags= NULL;
    static float *fft_window = NULL;
    float g = svf_g(880.f);
    float R = 1.f/QBUTTER;
    float R1 = R * SVF_24_R_MUL1;
    float R2 = R * SVF_24_R_MUL2;
if (!fft_setup) {
        fft_setup = pffft_new_setup(FFT_SIZE, PFFFT_REAL);
        fft_work = (float *)pffft_aligned_malloc(FFT_SIZE * 2 * sizeof(float));
        fft_buf = (float *)pffft_aligned_malloc(FFT_SIZE * sizeof(float));
        fft_mags = (float *)pffft_aligned_malloc(FFT_SIZE/2 * sizeof(float));
        fft_window = (float *)pffft_aligned_malloc(FFT_SIZE * sizeof(float));
        const float a0 = 0.42f, a1 = 0.5f, a2 = 0.08f;
        const float scale = (float)(2.0 * M_PI) / (float)(FFT_SIZE - 1);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float x = i * scale;
            fft_window[i] = a0 - a1 * cosf(x) + a2 * cosf(2.0f * x);
        }
        memset(fft_mags, 0, FFT_SIZE/2 * sizeof(float));
        float state[8]={};
        int numiter = 256;
        for (int iter=0;iter<numiter;iter++) {
            for (int i=0;i<FFT_SIZE;i++) {
                float y = rndt();
                y=svf_process_2pole(state, y, g, R).hp;
                y=svf_process_1pole(state+2, y, g).hp;
                fft_buf[i] = y * fft_window[i];
            }
            pffft_transform_ordered(fft_setup, fft_buf, fft_buf, fft_work, PFFFT_FORWARD);
            for (int i=0;i<FFT_SIZE/2;i++) {
                fft_mags[i] += (fft_buf[i * 2] * fft_buf[i * 2] + fft_buf[i * 2 + 1] * fft_buf[i * 2 + 1]) / (FFT_SIZE*16);
            }
        }
    }




    float prevy=0.f;
    float prevx=0.f;
    float prevy2=0.f;
    int dx=16;
    for (int bin = 4; bin < FFT_SIZE/4; bin++) {
        float freq = bin * (float)SAMPLE_RATE / (float)FFT_SIZE;
        float x = log2f(freq/48.f)*200.f;
        float signal_mag = fft_mags[bin];
        float signal_mag_db = squared2db(signal_mag);
        svf_gain_output_t gain1 = calculate_svf_gain_2pole(freq, g, R);
        svf_gain_output_t gain2 = calculate_svf_gain_1pole(freq, g);
        float hp = abs(gain1.hp * gain2.hp);
        float db = lin2db(hp);
        float y = db * -5.f + 500.f;
        float y2 = signal_mag_db * -5.f + 500.f;
        if (x) {
            add_line(x, y, prevx, prevy, 0xffffffff, 4.f);
            add_line(x, y2, prevx, prevy2, 0xff0000ff, 4.f);
        }
        prevy = y;
        prevy2 = y2;
        prevx = x;
    }
    printf("%f ", squared2db(fft_mags[1]));
    printf("%f\n", squared2db(fft_mags[FFT_SIZE/4]));

}
