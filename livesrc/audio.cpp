STATE_VERSION(1,  )

#ifdef PATTERNS

 // /simple_pattern <[c a f e] ^ 1 sus 0 dec 0.3 rel .3 att 0, [c5 c5 c5 c5] sus 0 dec 0.2>
 
/simple_pattern break_think fit loops 0 loope 1

/bpm 140
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(stereo inp) {
	F drums = 0. * sclip(rompler("break_think")*2); 
    //return stereo{drums,drums};
    stereo mix = test_patterns("/simple_pattern") * 4.;
	//mix+=reverb(mix*0.25f);
	return mix+drums;
}


