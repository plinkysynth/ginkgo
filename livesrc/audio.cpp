STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern <[c4 [a5 a3 a2] [f5 f4] [e4 c4]]!4 [[e4 e5] [g3 g5] [b3 b4] e4]!4>:2, 
	<[a1*4]!4 [g1*4]!3 [e1*4]!1>, <[a1*4]!4 [g1*4]!3 [e1*4]!1>

/bpm 120

#endif


stereo do_sample(stereo inp) {
	F drums = sclip(rompler("break_think")*5); 
    F t = test_patterns()*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}
