
#ifdef PATTERNS
/snare < a2-c3:garden_cp, sd:8 ^ 0.5  > sus 0 gain 0.7 dec 0.2
// /pattern [ foley?: randi 500 sus 0 dec 0.3 ^ 0.1 - foley: randi 500 sus 0.3] * 8 rib 500 4 mask {1 1 1}%2 gain 0.5
// , bd:4 struct [1 [0 - 0] - 0 0 0 [- - 1] -] dec 0.5 sus 0 gain 3,
// [ - hh from 1 to 0 /snare gain 2 - - [/snare? ^ 0.2 gain 0.2 - -] /snare gain 1 [- - /snare? ^ 0.2]],
//  break_riffin/2 fit 

// //brevenmore/2 fit
///piano <- a3(3 8 1)/2>:-1--0.5 sus 0 dec 0.3 $ add C5 gain 1
///piano [midi:-1 ^ 0.5 ] : cmajPent
// /piano <e5^'wvmZR'@0x8-0x4 c5^'wvmZR'@0xa-0x6>

//  sus 0 dec 0.1-0.5
// /piano <[c2 _ f2 c2 g2 c3 - c3]*2 add <0> dec sin * 0.83 range 0.2 1 sus 0-0.1 
// cut sin*0.333 range 640 200 res sin*0.252 
// range 0.7 0.95 dist 0.1 string 2 glide 0.1 noise 0.>

/foley [foley?:randi 500 sus 0 dec 0.3 ^ 0.3 foley?:randi 500 sus 0 dec 0.3]*8 rib 11 1 gain 0.5
/chords 
	// <c5 c5 c5  ds5 d5 d5 f5 [- d5] > struct [1(3 8) 1(3 8) 1(5 8) 1(3 8 2)]/2 $ :[-1--0.5*8] dec 0.1 sus 0.5 ,
    // [[ds2,c3,c2]^0.1 add 1]:sqrpad:2 stretch 0.2 att 0.5 rel 0.5 cut sin/10 range 1000 3000 vib 0.1 
/kick //bd:4 struct [1 1 1 1] dec 0.7 sus 0 gain 1.2
/drums1 [- /snare]*2, 
 		[- hh:3 ^ 0.2]*4
/drums2 [- /snare - -], [- hh:3 ^ 0.2]*4, [-@7 oh:1 from 1 to 0 rel 0]
/drums /foley, </drums1 ! 7 /drums2>
// /bass [c2 - [c2 c4^0.1 c3] [- c2]] add <0 3 7 [10 -2]>/2 $ : pluckng
/sub1 <a1 [d1 a1] [d1 a1] e1> add [-0.11-0,0.1] add 0 $ : -0.7 cut 200 string <0,1> glide 0.3 dist 0.3
/sub2 <[fs1 e1] [d1 a1] [d1 a1] e1> add [-0.11-0,0.1] add 0 $ : -0.7 cut 200 string <0,1> glide 0.3 dist 0.3
/chords1 <amaj [dmaj amaj] [dmaj amaj] emaj>
/chords2 <[fsmin emaj] [dmaj amaj] [dmaj amaj] emaj>

/v0 [- - - se:0 late 0xc8 rel 0.1]
/v1 [se from 0.15 se:1 late 0xc - se:2 late 0xc rel 0.1]/2
/v2 [se:2 from 0.06 - - se:3 late 0xc rel 0.1]/2
/v3 [se:3 from 0.13 se:4 late 0xc - se:5 late 0xb8 rel 0. ]/2
/v4 [se:5 from 0.06 - - se:6 late 0xc rel 0.1]/2
/v5 [se:6 from 0.067 - - se:7 late 0xc rel 0.1]/2
/v6 [se:7 from 0.055 - - se:8 late 0xb8 rel 0.1]/2
/v7 [se:8 from 0.07 - - se:9 late 0xc rel 0.1]/2
/v8 [se:9 from 0.065 - - se:0 late 0xb8 rel 0.1]/2

/vocals1 </v1@2 /v2@2 /v3@2 /v4@2>
/vocals2 </v5@2 /v6@2 /v7@2 /v8@2>
/sub </sub1@8 /sub2@8>
/vocals </vocals1@8 /vocals2@8>
/chords </chords1@8 /chords2@8>

/arps <[c1-c4*64] : /chords $ sus 0>

///piano <c2 ds2 g2 as2 >:-1  string 0 glide 0.5 rel 0.1 sus 1 dec 0.1 att 0.1
/bpm 80
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	

  synth_t _sub,_drums,_chords,_kick,_bass,_vocals;
  env_follower_t e;
  delay_t _delay;
	plinkyverb_t _reverb;
    bitcrush_t _bitcrush;
    stutter_t _stutter;
   
   stereo do_sample(stereo inp) {
   stereo kick = _kick("/kick", 1.f);
     stereo drums = vu(0,_drums("/drums", 1.f, {.dist=0.2})); // , {.distortion=0.3f, .debug_draw=true}));
     
	stereo sub = _sub("/sub", 0.5, {.dist=0.});
    stereo bass = _bass("/bass", 1., {.dist=0.});
    stereo chords = _chords("/arps", 0.7f, {.debug_draw=false});
    stereo vocals = _vocals("/vocals", 0.5f);
    stereo delay={};
    stereo reverb={};
    delay=_delay(chords, 0.5) * 0.125;
    reverb = _reverb(chords * 0.3f + vocals * 0.1f);

	stereo x = sub + drums + chords + bass + reverb * 0.5 + delay + vocals;
      float sidechain = e(kick);
      x/=max(1.f, 4.f*(sidechain)); // -0.02f
      x+=kick;
        
    //x=bitcrush(x, 0.5);
    //x=stutter(x, 32, 8);
        x=ott(x * 0.5, 0.5) * 2.f;
//  x=x*2.;      
        vu(7,x);
      return x;
  }
};
