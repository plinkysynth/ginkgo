STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern < # [a b c d e f g a] [b c b c b c b c b c b c b c] # e1 >
/up [c e]
/down [g f e d]






/bpm 140

#endif


stereo do_sample(stereo inp) {
	F drums = 0 * sclip(rompler("break_think")*2); 
    F t = test_patterns()*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}


