STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern [c a f e]

/bpm 120

#endif


stereo do_sample(stereo inp) {
	F drums = 0 * sclip(rompler("break_think")*5); 
    F t = test_patterns()*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}
