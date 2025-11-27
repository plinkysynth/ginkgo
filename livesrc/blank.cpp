
#ifdef PATTERNS

/pattern [bd@0-1 sd@1-1]

/bpm 140

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.75);
      //x+=reverb(x*0.5f);
      return x;
  }
};
