
#ifdef PATTERNS
/fancy_pattern [c4 ! 3 d]
#endif


STATE_VERSION(1,  )

void init_state(void) {
}

float test_patterns(void);

float rompler(const char *fname) {
	wave_t *wave=get_wave_by_name(fname);
	return (wave && wave->frames) ? wave->frames[((G->sampleidx/2) % (wave->num_frames))*wave->channels] : 0.f;
}


stereo do_sample(stereo inp) {
   F t = test_patterns()*0.5;
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
  //  F drums = sclip(rompler("break_think") * pow4(S4(0))*10.); 
  //  F dc = S5(0) * 2.;
  //  F mixed = chord1;
  //   stereo dry=stereo{mixed*0.5f,mixed*0.5f};
     stereo wet=reverb(dry*0.5f);
    wet = wet*0.5+dry*1.;//+drums*0.5f;
    
    return probe = wet;// + dc;
}
