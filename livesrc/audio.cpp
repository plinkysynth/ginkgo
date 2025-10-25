STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern #
c4,e4,g4,c5

[c d e]*4

g



a,f4,c4



b,g4



#,#
c2
c3
c2
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


