STATE_VERSION(1,  )

#ifdef PATTERNS

 // /simple_pattern <[c a f e] ^ 1 sus 0 dec 0.3 rel .3 att 0, [c5 c5 c5 c5] sus 0 dec 0.2>
 
/crunch misc:1? | - | shaker_large:4 
/rim AkaiXR10_sd:0 gain 1.2 sus 0 dec 0.2
/drum_pattern [- - /crunch - /rim - rim:3 gain 0.4 subroc3d:7 dec 0.05-0.2 sus 0 ], [ - hh - hh - hh hh? hh ], [/crunch]*16,
[bd - - - - - bd gain 0.3 - - bd - - - bd gain 0.4 -], /cafe

/cafe [c a f e] sus 0 dec 0.3  gain 0.25

/bpm 140
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(stereo inp) {
	F drums = 0. * sclip(rompler("break_think")*2); 
    //return stereo{drums,drums};
    stereo mix = sclip(test_patterns("/drum_pattern") * 3.);
    //stereo mix = test_patterns("/cafe");
	mix+=reverb(mix*0.25f);
	return mix+drums;
}


