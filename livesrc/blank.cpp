
#ifdef PATTERNS




///pattern <[c,e,g]*8 [a2,c,e]*8 [f,a,c4]*8> arp [0 4 6 4 5 4 7 <- 2>] sus 0 dec 0.3-0.9 
/pattern emaj arp 0

/bpm 140

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.75);
      x+=reverb(x*0.5f);
      return x;
  }
};
