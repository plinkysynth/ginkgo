
#ifdef PATTERNS

/pattern <<a1,c2,e2,a0,c4 f1,c2,a2,f0,a3 b0,a1,f2,f3 e1,b1,e3> add 1> / 2 : sqrpad:2 
 	stretch 0.1 jitter 0.5 grain 2 gain 1 rel 0.7 att 0.7,
 	<a1 f1 f1 e1>/2 :-1  att 0.5 rel 0.5 gain 0.5

// /piano [<c4 - f4 g4 - d4 ds4 [bb3 f3]> *6 add 9]  : [pianosg4|pianosg2] gain 6
/piano [[[a3-a5 * 8]?: <aminPent fmajPent fmajPent gmajPent>/2] rib 13 8] : pianosg4 gain 4
///pattern c3 : sqrpad:2, c3

///pattern c3:sqrpad:2,b3

/bpm 140
/* foobar */
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1,s2;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = s1("/pattern", 0.5);
		x += s2("/piano", 0.5);
      x+=reverb(x * 0.1f);
      return x;
  }
};
