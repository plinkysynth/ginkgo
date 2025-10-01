//   __ _ _   _  __| (_) ___
//  / _` | | | |/ _  | |/ _ \
// | (_| | |_| | (_| | | (_) |
//  \__,_|\__,_|\__,_|_|\___/



#ifdef PATTERNS

// any block comment beginning "#ifdef PATTERNS" will be considered pattern area.
// this is the pattern area. it is parsed and 'compiled' by gingko, not the c compiler.
// we create named mini-notation patterns and curves.

// this is a graph - any string inside single quotes becomes a value you can edit as both text and visually.
// the graph appears when you hover it.

' .      ^      =               _      '     


[[sd rim] bd(3,8)]


#endif


STATE_VERSION(1, bq_t lpf, hpf; )

void init_state(void) {
    G->lpf = bqlpf(0.0625, QBUTTER);
    G->hpf = bqhpf(0.025, QBUTTER);
}

stereo do_sample(stereo inp) {
    F chord1 = sawo(P_C3) + sawo(P_E4) + sawo(P_G4);
    F chord2 = sawo(P_C3) + sawo(P_F4) + sawo(P_A4);
    F chord3 = sawo(P_G3) + sawo(P_D3) + sawo(P_B3);
    chord1 = chord1 * vol(S0) + 
        chord2 * vol(S1) +
        chord3 * vol(S2);
   F out = lpf4(chord1, 0.3);
    //out = squareo(P_E4) * 0.5;
    
    float t= exp2f(-0.0002f * (G->sampleidx&65535));
    out += sino(P_A5) * t;
   	stereo dry=STEREO(out,out);
    //return dry;
    return stadd(dry,reverb(dry));
}
