
#ifdef PATTERNS

///pattern break_riffin fitn 2 gain cos
/breakz [break_riffin*8] fitn 16
    	from [[0 1 2 3 4 5 6 7] floor div 16] pan 0.5

/pattern bd hh sd hh, /breakz

/bpm 130.0
// bpm 76 breaks it on beat 0.20 ie at time 0.5!
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {
	synth_t synth;
  delay_t delay;
  reverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = synth("/pattern", 1.f) * 5.;
      x += delay(x, st(1.5,1.), 0.75, 1.2) * 0.25;
      //x+=reverb(x*0.04f + preview * 0.1f);
      // final vu meter for fun
      envfollow(x.l);
      envfollow(x.r);
      return x;
  }
};
