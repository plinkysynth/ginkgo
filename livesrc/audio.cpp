
#ifdef PATTERNS

/pattern bd hh sd hh 

/bpm 130
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
STATE_VERSION(1,  
	synth_state_t synth;
    delay_state_t delay;
    reverb_state_t reverb;
)

stereo do_sample(stereo inp) {
    stereo x = synth(&G->synth, "/pattern", 1.f);
    //x += delay(&G->delay, x, st(1.5,1.), 0.75, 1.2) * 0.25;
  	//x+=reverb(&G->reverb, x*0.04f + G->preview * 0.1f);
    // final vu meter for fun
    envfollow(x.l);
    envfollow(x.r);
    return x;
}




