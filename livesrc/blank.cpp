
#ifdef PATTERNS

///pattern break_riffin fitn 2 gain cos
/zpattern [bd!15 [- bd]]/4
	, [- hh:18 gain 0.8-1]*4
   , [- clap]*2 gain 1
      , [[- rim:0 - -] late 0.2]
        , [- - - - - bd ^ 0.25 - -] ,
  
/ropattern [[c|c4] [a|a4] [f|f4] [e|e5]] cut [[sin * 0.1] range 300 1800] res 0.7 sus 0 dec 0.3 gain 2
/pattern /ropattern, /ropattern * 0.33333 sub 12, /ropattern * 1.5 add 12

/apattern [c a f e] : -1

/bpm 120.0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {
  synth_t s1,s2;
  delay_t delay;
  reverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.2f);
      x += delay(x, st(0.75,0.75), 0.9, 2.);
      x+=reverb(x*0.4f + preview * 0.1f);
      x+=s2("/zpattern", 1.);
      //x+=inp*10.;
      // final vu meter for fun
      envfollow(x.l);
      envfollow(x.r);
      return x;
  }
};
