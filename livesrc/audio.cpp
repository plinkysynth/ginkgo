STATE_VERSION(1,  )

#ifdef PATTERNS

 // /simple_pattern <[c a f e] ^ 1 sus 0 dec 0.3 rel .3 att 0, [c5 c5 c5 c5] sus 0 dec 0.2>
 
/simple_pattern /* hello */ <a4 g4 d4 f4>*4 sus 1 rel 0.2 att 0.05 clip 0.1-0.5, d1 gain 
	/*0======2*/ 0.84

/bpm 130
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(stereo inp) {
	F drums = 1. * sclip(rompler("break_think")*2); 
    //return stereo{drums,drums};
    stereo mix = test_patterns("/simple_pattern") * 0.5;
	mix+=reverb(mix*0.5f);
	return mix+drums;
}


