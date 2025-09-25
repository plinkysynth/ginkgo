//   __ _ _   _  __| (_) ___
//  / _` | | | |/ _` | |/ _ \
// | (_| | |_| | (_| | | (_) |
//  \__,_|\__,_|\__,_|_|\___/
//


STATE_VERSION(1, )

stereo do_sample(stereo inp, U sampleidx) {
    F chord1 = sawo(P_C3) + sawo(P_Eb4) + sawo(P_G4);
    F chord2 = sawo(P_C3) + sawo(P_F4) + sawo(P_A4);
    F chord3 = sawo(P_G3) + sawo(P_D3) + sawo(P_B3);
    chord1 = chord1 * S0 + 
        chord2 * S1 +
        chord3 * S2;
    F out = lpf4(chord1, 0.3);
    //out = sino(0.125);
    return (stereo){out * 0.25, out * 0.25};
}




























