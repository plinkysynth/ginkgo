
#ifdef PATTERNS
/pattern bd
/bpm 123.3
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	

  synth_t _pattern;
   
   stereo do_sample(stereo inp) {
    return _pattern("/pattern", cc(0));
  }
};
