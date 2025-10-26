STATE_VERSION(1,  )

#ifdef PATTERNS
/fancy_pattern <#
e3,gs3,b3,gs5
[e2,e1]*16
e3,gs3,b3,gs5
[e1,e2]*8
a3,cs3,e3,a5
[a1,a2]*8
e4,gs3,b3,e5
[e1,e2]*16
g3,bb3,d4,d5
[g1,g2]*8
c4,e4,g3,b3,c5
[c1,c2]*8
e4,g3,b3,e5
[e1,e2]*2
d3,fs3,a3,fs5 
[d2,d1]*2
#>/2
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(stereo inp) {
	F drums = 1 * sclip(rompler("break_think")*2); 
    F t = test_patterns("/fancy_pattern")*0.2;
	stereo dry=stereo{t,t};
	stereo wet=reverb(dry*0.5f);
	return wet*0.5+dry*1.+drums;
}


