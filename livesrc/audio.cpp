
STATE_VERSION(1,  )

void init_state(void) {
}

float test_patterns(void);

float rompler(const char *fname) {
	wave_t *wave=get_wave_by_name(fname);
	return (wave && wave->frames) ? wave->frames[((G->sampleidx/2) % (wave->num_frames))*wave->channels] : 0.f;
    
}

#ifdef PATTERNS
/fancy_pattern <[c4 [a5 a3 a2] [f5 f4] [e4 c4]]!4 [[e4 e5] [g3 g5] [b3 b4] e4]!4>:2, 
	<[a1*4]!4 [g1*4]!3 [e1*4]!1>, <[a1*4]!4 [g1*4]!3 [e1*4]!1>

/bpm 124.5

#endif


stereo do_sample(stereo inp) {
F drums = sclip(rompler("break_think")*5); 
   F t = test_patterns()*0.2;
     stereo dry=stereo{t,t};
  //    return dry2;
  // //return stereo{t,t};
  // //F t = rndt();
  // //t=peakf(t, 0.1f, 880.f, 1.f);
  // //t=lpf(t, 880.f, 40.f);
  // //t=sino(P_A4);
  // //return probe=stereo{t,t};  
  
  // F chord1 = sawo(P_C3) + sawo(P_Ds4) + sawo(P_C4) + pwmo(P_C1,0.25) + sino(P_C5) + sino(P_C6);
  //   F chord2 = sawo(P_Gs2) + sawo(P_F4) + sawo(P_C4) + pwmo(P_F1,0.25) + sino(P_F5) + sino(P_Ds6);
  //   F chord3 = sawo(P_D3) + sawo(P_B3) + sawo(P_G2) + pwmo(P_G1,0.25) + sino(P_G5);
  //   //return stereo{sino(P_C5)};
  //   chord1 = chord1 * vol(slew(S0(0.5))) + 
  //       chord2 * vol(slew(S1(0.))) +
  //       chord3 * vol(slew(S2(0.)));
  //  //F bass = sclip(lpf_dp(sawo(P_C1)+sawo(P_C2*1.03324f)+sawo(P_C2*0.99532f), P_C3, 0.1f))*0.2; // growly bass    
  //  
  //  F dc = S5(0) * 2.;
  //  F mixed = chord1;
  //   stereo dry=stereo{mixed*0.5f,mixed*0.5f};
     stereo wet=reverb(dry*0.5f);
    wet = wet*0.5+dry*1.+drums;
    
    return probe = wet;// + dc;
}
