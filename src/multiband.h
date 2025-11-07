// computed by testlrsplitter.ipynb for 125hz / 2500hz cutoffs at 96000hz
static const float lowpass_b0 = 1.6636798430524727e-05f; // and b2
static const float lowpass_b1 = 3.3273596861049454e-05f;
static const float lowpass_a1 = -1.9884301203029076f;
static const float lowpass_a2 = 0.9884966674966296f;

static const float highpass_b0 = 0.890724065398853f; // and b2
static const float highpass_b1 = -1.781448130797706f;
static const float highpass_a1 = -1.7694710382281469f;
static const float highpass_a2 = 0.793425223367265f;

#define allpass_lo_b0 lowpass_a2
#define allpass_lo_b1 lowpass_a1
#define allpass_hi_b0 highpass_a2
#define allpass_hi_b1 highpass_a1

// b0 and b2 are the same for low and high pass...
#define process_biquad_low_or_high(y, x, state0, state1, b0and2, b1, a1, a2)                                                       \
    {                                                                                                                              \
        stereo xb02 = x * b0and2;                                                                                                  \
        y = state0 + xb02;                                                                                                         \
        state0 = x * b1 - y * a1 + state1;                                                                                         \
        state1 = y * -a2 + xb02;                                                                                                   \
    }

// b2 is 1, b1 and a1 are the same, b0 is the same as a2
#define process_allpass(y, x, state0, state1, b0anda2, b1anda1)                                                                    \
    {                                                                                                                              \
        y = x * b0anda2 + state0;                                                                                                  \
        state0 = (x - y) * b1anda1 + state1;                                                                                       \
        state1 = x - y * b0anda2;                                                                                                  \
    }

void multiband_t::operator()(stereo x, stereo out[3]) {
    stereo low, high, allpass, low2, high2, allpass2;
    process_biquad_low_or_high(low, x, state[0], state[1], lowpass_b0, lowpass_b1, lowpass_a1, lowpass_a2);
    process_allpass(allpass, x, state[2], state[3], allpass_lo_b0, allpass_lo_b1);
    process_biquad_low_or_high(low2, low, state[4], state[5], lowpass_b0, lowpass_b1, lowpass_a1, lowpass_a2);

    process_biquad_low_or_high(high, x, state[6], state[7], highpass_b0, highpass_b1, highpass_a1, highpass_a2);
    process_allpass(allpass2, allpass, state[8], state[9], allpass_hi_b0, allpass_hi_b1);
    process_biquad_low_or_high(high2, high, state[10], state[11], highpass_b0, highpass_b1, highpass_a1, highpass_a2);
    stereo mid2 = allpass2 - low2 - high2;
    out[0] = low2;
    out[1] = mid2;
    out[2] = high2;
}

// inf upward, 4:1 downward
inline float compgain(float inp, float lothresh, float hithresh, float makeup) {
	if (inp > hithresh) return min(32.f,sqrtf(sqrtf(hithresh/inp)) * makeup);
    if (inp < lothresh) return min(32.f,lothresh/inp * makeup);
    return makeup;
}


stereo ott_t::operator()(stereo rv, float amount) {
    stereo bands[3];
    // +5.2 on input then
    const float inp_gain = db2lin(12.2);
    multiband(rv * inp_gain, bands);
    // thresh, decay, attack
    float b=envfollow_novu(bands[0],0.02f, 0.01f, 0.001f);
    float m=envfollow_novu(bands[1],0.02f, 0.01f, 0.001f);
    float h=envfollow_novu(bands[2],0.02f, 0.01f, 0.001f);
    // -40.8 : -35.5 for hi,  inf:1 / 4:1
    // -41.8 : -30.2 for mid 66 : 1 / 4:1
    // -40.8 : -33.8 for lo  66 : 1 / 4:1
    // +10.3 out for lo/hi, +5.7 out for mid
    const float lothreshb = db2lin(-40.8),hithreshb=db2lin(-35.5);
    const float lothreshm = db2lin(-41.8),hithreshm=db2lin(-30.2);
    const float lothreshh = db2lin(-40.8),hithreshh=db2lin(-33.8);
    const float makeupb = db2lin(10.3);
    const float makeupm = db2lin(5.7);
    const float makeuph = db2lin(8.3);
    float bgain = lerp(1.f/inp_gain, compgain(b,lothreshb,hithreshb, makeupb), amount);
    float mgain = lerp(1.f/inp_gain, compgain(m,lothreshm,hithreshm, makeupm), amount);
    float hgain = lerp(1.f/inp_gain, compgain(h,lothreshh,hithreshh, makeuph), amount);
    rv=sclip(bands[0] * bgain + bands[1] * mgain + bands[2] * hgain);
    return rv;
}
