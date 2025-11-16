
#ifdef PATTERNS

/breakz break_riffin/2 dist 0 fitn 1
/zpattern [[bd!15 [- bd]]/4 gain 0.5
	 , [- hh:18 gain 0.8-1]*4
    , [- clap]*2 gain 1
       , [[- rim:0 - -] late 0.2] 
         , [- - - - - bd ^ 0.25 - -]] dist 0.1


/ropattern [c a f e] blend /*=======*/cc(0) [c4 c4 c4] sus 0
	// [[[g3-c6]?0.3*12] rib 1 0.5 : sin * 0.0437 range 1. 1.4 : 
    // <aminPent cmajPent fmajPent gmajPent> sus 0.3-0.7  
   
    // sus2 0. res sin*0.1 range 0.4 0.7 att 0. dec 0.1-0.3
    //  rel 0.1 res 0.1-0.5 sus2 sin*0.15 range 0.1 0.5 
    //  sus sin*0.26 range 0.3 0.9  gain 0.3 cut 7000 pan 0.4-0.6],
    // //<[a1 a2 a1 a2] [c1 c2] [f1 f2 f1 f2] [g1 g2 g1 g2]> sus 0.8 cut 800 gain 0.4 string 0 glide 0.3
    // //<a1 c2 f1 g1>:0 sus 0.9 cut 800 gain 0.3 dist 0.3 string 0 glide 0.6
    













/pattern /ropattern

/bpm 100.0
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {
  synth_t s1,s2;
  delay_t delay;
  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = (s1("/pattern", 0.75));
      x += delay(x, st(0.75,1.), 0.75)*0.3;
      x+=reverb(x*0.1 + preview * 0.1f);
     // x+=s2("/zpattern", 0.5);
      //x+=s2("/breakz",0.5);
      //x+=inp*10.;
      // final vu meter for fun
      envfollow(x.l);
      envfollow(x.r);
      return x;
  }
};


void xupdate_frame(void) { 
  set_camera(float4{0.f,0.f,-12.f,1.f}, float4{0.f,0.f,0.f,1.f}, 0.6f); 
}
