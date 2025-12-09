
#ifdef PATTERNS
/snare <a2-c3:garden_cp sus 0 dec 0.2, sd:2 sus 0 gain 0.5 dec 0.1 > gain 1
/pattern [ foley?: randi 500 sus 0 dec 0.5 ^ 0.1 - foley: randi 500 sus 0.5] * 16 rib 12 4 mask {1}%2 gain 0.5
, bd:4 struct [1 [0 - 0] - 1 - 0 [- - 0] -] dec 0.5 sus 0 gain 1, 
[ - - /snare gain 2 - - [/snare? ^ 0.2 gain 0.2 - -] /snare gain 1 [- - /snare? ^ 0.2]]
// /pattern midi : -1
/piano [c3 a3 f3 e3]:-1 sus 0 dec 0.9

//  sus 0 dec 0.1-0.5
// /pattern br160/4:2 fit
/bpm 100
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	

  synth_t s1,s2,s3;
  env_follower_t e;
	  plinkyverb_t reverb;
    bitcrush_t bitcrush;
    stutter_t stutter;
  stereo do_sample(stereo inp) {
  //stereo d = vu(2,s3("/drums", cc(2)));
     stereo d = vu(0,s1("/pattern", 1.f)); // , {.distortion=0.3f, .debug_draw=true}));
	stereo x = s2("/piano", 1., {.debug_draw=true});
      x+=vu(6,reverb(x * 0.4f));
      //float sidechain = e(d);
      //x/=max(1.f, 10.f*(sidechain)); // -0.02f
		x+=d;
        
    //x=bitcrush(x, 0.5);
    //x=stutter(x, 32, 8);
        x=ott(x * 0.5, 0.7) * 2;
  //x=x*5.;      
  //      vu(7,x);
      return x;
  }
};
