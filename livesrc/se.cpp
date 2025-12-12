
#ifdef PATTERNS
/chords1 <amaj [dmaj amaj] [dmaj amaj] emaj>
/chords2 <[fsmin emaj] [dmaj amaj] [dmaj amaj] emaj>
/roots1 <a [d a] [d a] e>
/roots2 <[fs e] [d a] [d a] e>

/v0 [- - - se:0 late 0xc8 rel 0.1]
/v1 [se from 0.15 se:1 late 0xc - se:2 late 0xc rel 0.1]/2
/v2 [se:2 from 0.06 - - se:3 late 0xc rel 0.1]/2
/v3 [se:3 from 0.13 se:4 late 0xc - se:5 late 0xb8 rel 0. ]/2
/v4 [se:5 from 0.06 - - se:6 late 0xc rel 0.1]/2
/v5 [se:6 from 0.067 - - se:7 late 0xc rel 0.1]/2
/v6 [se:7 from 0.055 - - se:8 late 0xb8 rel 0.1]/2
/v7 [se:8 from 0.07 - - se:9 late 0xc rel 0.1]/2
/v8 [se:9 from 0.065 - - se:0 late 0xb8 rel 0.1]/2

/chords </chords1@8 /chords2@8>
/roots </roots1@8 /roots2@8>
/vocals1 </v1@2 /v2@2 /v3@2 /v4@2>
/vocals2 </v5@2 /v6@2 /v7@2 /v8@2>

/drums /foley, </drums1 ! 7 /drums2>
/snare < a2-c3:garden_cp, sd:8 ^ 0.5  > sus 0 gain 0.7 dec 0.2

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// [foley?:randi 500 sus 0 dec 0.3 ^ 0.3 foley?:randi 500 sus 0 dec 0.3]*8 rib 11 1 gain 0.5
// /roots add [-24.11--24,-24.1] $ : -0.7 cut 200 string <0,1> glide 0.3 dist 0.3

//cc0
/kick <bd(2 8):4 bd(3 8):4>  dec 0.7 sus 0 gain 1.2

//cc1
/foley [foley?:randi 500 sus 0 dec 0.3 ^ 0.3 foley?:randi 500 sus 0 dec 0.3]*8 rib 22 1 gain 0.5
/drums1  [- /snare]*2, 
 		 [- hh:3 ^ 0.2]*4
/drums2  [- /snare - -], [- hh:3 ^ 0.2]*4, [-@7 oh:1 from 1 to 0 rel 0]

//cc2
/sub /roots add -24 $ : -0.7 cut 200
//cc3
/bass [c2 - [- g2^0.5-0.3 c3] [- c2]] add /roots $ : pluckng

//cc4
/arps <[c7-a3?0.7*16 pan [rand*16]] rib 3 2 $ : /chords $ sus 0 $ :-1--0.5 dec 0.3-0.2 att 0.051 > gain 0.7 
/pads <midi: cmajor>/2 $ add a2  $ cut sin/5 range 1000 2000 gain 0.4 att 0.2 rel 0.5 lpg 1 glide 0.2 trem 1
// 1 - midi interface
// 20 - cheap
// 7 - bass & drums
// 10 - difficult?
// 11, 13- simple , easy
// 29,30,31,34 - flexible text
// 33 wordproc
// 35 musical coder
// 14 start again
// 18 had enough
// cc5
/vox <microlive:<- 29 - 30 - 31 - 34 - 33> >/2 rel 0 gain 2
///vox <microlive:<18 14> late 0.125 >/2 rel 0

/vocals </vocals1@8 /vocals2@8> gain 1 $ mask <1 1>/2

/bpm 80
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {	

  synth_t _sub,_drums,_chords,_kick,_bass,_vocals,_pads,_vox;
  env_follower_t e;
  delay_t _delay;
	plinkyverb_t _reverb;
    bitcrush_t _bitcrush;
    stutter_t _stutter;
   
   stereo do_sample(stereo inp) {
   stereo kick = vu(0,_kick("/kick", cc(0)));
    stereo drums = vu(1,_drums("/drums", cc(1), {.dist=0.2})); // , {.distortion=0.3f, .debug_draw=true}));
	stereo sub = vu(2,_sub("/sub", cc(2), {.dist=0.}));
    stereo bass = vu(3,_bass("/bass", cc(3), {.dist=0.}));
    stereo chords = _chords("/arps", cc(4), {.debug_draw=false});
    chords += _pads("/pads", cc(4));
    vu(4,chords);
     stereo vox = vu(5, _vox("/vox", cc(5)));
    stereo vocals = vu(6,_vocals("/vocals", cc(6)));
    stereo delay={};
    stereo reverb={};
    delay=_delay(chords + vox * 0.2, 0.75) * 0.25;
    reverb = _reverb(chords * 0.1f + vocals * 0.1f);

	stereo x = sub + drums + chords + bass + reverb * 0.5 + delay + vocals + vox;
      float sidechain = e(kick);
      x/=max(1.f, 4.f*(sidechain)); // -0.02f
      x+=kick;
        
    //x=bitcrush(x, 0.5);
    //x=stutter(x, 32, 8);
       x=ott(x * 0.5, 0.5) * 2.f;
       x = x * 2.5f * cc(7);
//  x=x*2.;      
        vu(7,x);
      return x;
  }
};
