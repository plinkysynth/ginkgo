STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern #
c4
[c d e f]
#


/bpm 140

#endif


stereo do_sample(stereo inp) {
	F drums = 0 * sclip(rompler("break_think")*2); 
    F t = test_patterns("/fancy_pattern")*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}


