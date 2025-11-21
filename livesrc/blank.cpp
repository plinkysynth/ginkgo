
#ifdef PATTERNS

/snare [- sd] * 4 gain y
/kick bd * 4 gain x
/hats [- hh] * 8 gain yellow
/pattern /snare, /kick, /hats

/bpm 101.4
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
