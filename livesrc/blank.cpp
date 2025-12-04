
#ifdef PATTERNS

/pattern <<a1,c2,e2,a0,c4 f1,c2,a2,f0,a3 b0,a1,f2,f3 e1,b1,e3> add 1 pan rand> / 2 
: sqrpad:2 stretch 0.1 jitter 0.5 grain 2 gain 1 rel 0.7 att 0.7,
 	<a1 f1 f1 e1>/2 :-1  att 0.5 rel 0.5 gain 0.7 

/piano [[a3-a6 * 8]? rib 3 2] : <aminPent fmajPent fmajPent gmajPent>/2 : pianosg4 gain 4 
/drums <br160:10>/4 fit
///pattern <<c e a2 g2> struct [X X X _]*4> ^ [1 0.6]*8 clip sin/4 range 0.1 0.6 
// /pattern [bd bd bd @2]*4

/pattern midi : amaj

/bpm 140
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1,s2,s3;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.5);
		x += s2("/piano", 0.5);
      x+=reverb(x * 0.1f);
      vu(0, x); 
      stereo d = s3("/drums", 1.);
      vu(1,d);
      x +=d;
      return x;
  }
};
