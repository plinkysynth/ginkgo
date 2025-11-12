
#ifdef PATTERNS

///pattern break_riffin fitn 2 gain cos
/zpattern [[bd!15 [- bd]]/4
	 , [- hh:18 gain 0.8-1]*4
    , [- clap]*2 gain 1
       , [[- rim:0 - -] late 0.2] 
         , [- - - - - bd ^ 0.25 - -]] dist 0.1
       

/ropattern [[c2 c3 c4 g3 g2 g4 c5 d4 ] add 12] : 1.3 : saw string 0 
cut [sin * 0.1 range 3000 18000] sus2 0.1-0.4 dec2 0.1-0.3 sus 0-1 dec 0.1-0.3 ^ 0-0.5
///ropattern [c3 string 0 glide 0.1 sus 0.1 att 0.1] : -1 // [[c a f e] , <- c4 d4 e4 f4 g4 a4 b4 c3> late 0.1, [c5|a5|f5|e5]*16 ] : piano, c2:-1
///ropattern [c a f e] : -1
/pattern /ropattern, c1 sus2 0.1 dec 0.3 : -0.4 // , /ropattern * 0.33333 sub 12, /ropattern * 1.5 add 12

/apattern [c a f e] : -1

/bpm 120.0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {
  synth_t s1,s2;
  delay_t delay;
  reverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 1);
      x += delay(x, st(0.75,1.5), 0.6, 0.5) * 0.3;
      x+=reverb(x*0.1f + preview * 0.1f);
      //x+=s2("/zpattern", 1.);
      //x+=inp*10.;
      // final vu meter for fun
      envfollow(x.l);
      envfollow(x.r);
      return x;
  }
};

