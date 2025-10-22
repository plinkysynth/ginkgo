STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern [[c3 _ c2 -] - d3 -  f3 - c6 f5 c4 ]







/bpm 140

#endif


stereo do_sample(stereo inp) {
	F drums = 0 * sclip(rompler("break_think")*2); 
    F t = test_patterns()*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}


