
#ifdef PATTERNS

///pattern break_riffin fitn 2 gain cos
/zpattern [[bd!15 [- bd]]/4 gain 1
	 , [- hh:18 gain 0.8-1]*4
    , [- clap]*2 gain 1
       , [[- rim:0 - -] late 0.2] 
         , [- - - - - bd ^ 0.25 - -]] dist 0.1

       
              
                     
                            
                                   
                                          
                                                 
                                                        
                                                               
                                                                      
                                                                             
                                                                                    
                                                                                           
                                                                                                         

/ropattern [c1 c2 c3 c4 c5 c6]:-1 loope sin*0.2 range 0.01 0.7












/pattern /ropattern

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
      //x += delay(x, st(0.75,1.5), 0.6, 0.5) * 0.3;
      //x+=reverb(x*0.1f + preview * 0.1f);
      //x+=s2("/zpattern", 1.);
      //x+=inp*10.;
      // final vu meter for fun
      envfollow(x.l);
      envfollow(x.r);
      return x;
  }
};


void update_frame(void) { 
  set_camera(float4{0.f,0.f,-8.f,1.f}, float4{0.f,0.f,0.f,1.f}); 
}
