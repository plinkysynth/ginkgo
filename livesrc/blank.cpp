
#ifdef PATTERNS

// /pattern <<a1,c2,e2,a0,c4 f1,c2,a2,f0,a3 b0,a1,f2,f3 e1,b1,e3> add 1 pan rand> / 2 
// : sqrpad:2 stretch 0.1 jitter 0.5 grain 2 gain 0.5 rel 0.7 att 0.7,
//  	<a1 f1 f1 e1>/2 :-0.5  att 0.5 rel 0.5 gain 0.7 cut 200

// /piano [[a3-a5 * 8]?1 rib 6 2, midi] : <aminPent fmajPent fmajPent gmajPent>/2 : pianosg4 gain 4 
/drums <br160:12>/4 fit

/piano midi : cmajor att 0.1 rel 0.5 cut 1500 vib <0.03,0.01> vibf 0.323996 glide 0.1 lpg 1

///pattern <<c e a2 g2> struct [X X X _]*4> ^ [1 0.6]*8 clip sin/4 range 0.1 0.6 
// /pattern [bd bd bd @2]*4

// /pattern midi : amaj

/bpm 140
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1,s2,s3;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
      stereo x = vu(0,s1("/pattern", cc(0)));
		x += vu(1,s2("/piano", 1., 16, true));
      x+=vu(6,reverb(x * 0.1f));
		x+=vu(2,s3("/drums", cc(2)));
        
        vu(7,x);
      return x;
  }
};
