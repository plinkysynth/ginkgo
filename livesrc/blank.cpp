
#ifdef PATTERNS

/patc [c*8]
/patf [f*4]
/pata [a*4]
/pattern blendnear [/pata /patc /patf] sus 0 dec 0.3

/bpm 140

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.75);
      x+=reverb(x*0.1f);
      return x;
  }
};
