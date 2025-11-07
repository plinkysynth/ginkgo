
#ifdef PATTERNS

///pattern break_riffin fitn 2 gain cos
/breakz [break_riffin*8] fitn 16
    	from [[0 1 2 3 4 5 6 7] floor div 16] pan 0.5

/pattern bd hh sd hh, /breakz

/bpm 130.0
// bpm 76 breaks it on beat 0.20 ie at time 0.5!
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
STATE_VERSION(1,  
	synth_state_t synth;
    delay_state_t delay;
    reverb_state_t reverb;
    float limiter_state[2];
)

stereo do_sample(stereo inp) {
    stereo x = synth(&G->synth, "/pattern", 1.f) * 5.;
    //x += delay(&G->delay, x, st(1.5,1.), 0.75, 1.2) * 0.25;
  	//x+=reverb(&G->reverb, x*0.04f + G->preview * 0.1f);
    x = limiter(G->limiter_state, x);
    // final vu meter for fun
    envfollow(x.l);
    envfollow(x.r);
    return x;
}




