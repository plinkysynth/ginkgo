
#ifdef PATTERNS

/pattern <<a1,c2,e2,a0,c4 f1,c2,a2,f0,a3 b0,a1,f2,f3 e1,b1,e3> add 1 pan rand range 0.3 0.7> / 2 
: sqrpad:2 stretch 0.1 jitter 0.5 grain 2 gain 0.5 rel 0.7 att 0.7,
 	<a1 f1 f1 e1>/2 :-0.5  att 0.5 rel 0.5 gain 0.5 cut 2000

/piano [[a3-a5 * 8]?0.4 rib 20 4] : <aminPent fmajPent fmajPent gmajPent>/2 : pianosg4 gain 2
// /piano [e4 e5 f5]/3 : pianosg4
// /piano [- - [e6 e7 e6 e5 c5 c4] e3 - - ] sus 0 dec 0.2 $ : -1 dist 0.3
/drums <
< <break_riffin/2 fit struct [1 1 2 1] from [0 0.125 0.25 0.375]> clip 0.9 sus 0.5>,
garden_bd * 4 gain 1, 
[- - garden_cp [garden_cp^0.2]] * 4 gain 0.5 sus 0, [- cp:4]*4 gain 0.>


// /piano midi : cmajor att 0.1 rel 0.5 cut 1500 vib <0.03,0.01> vibf 0.323996 glide 0.1 lpg 1

// /piano c3:-1 sus 0 gain 0 dec 0.3

///pattern <<c e a2 g2> struct [X X X _]*4> ^ [1 0.6]*8 clip sin/4 range 0.1 0.6 
// /pattern [bd bd bd @2]*4

// /pattern midi : amaj

/bpm 140
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	
  synth_t s1,s2,s3;
  env_follower_t e;
	  plinkyverb_t reverb;
  stereo do_sample(stereo inp) {
  stereo d = vu(2,s3("/drums", cc(2)));
      stereo x = vu(0,s1("/pattern", cc(0)));
		x += vu(1,s2("/piano", cc(1), 8));
      x+=vu(6,reverb(x * 0.1f));
      float sidechain = e(d);
      x/=max(1.f, 40.f*(sidechain-0.01f)); // -0.02f
		x+=d;
        
        x=ott(x * 0.5, 0.5) * 5;
        vu(7,x);
      return x;
  }
};
