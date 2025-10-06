
#ifdef PATTERNS
/fancy_pattern [[- rim], bd(3,8), hh*8]

/kick44 [bd*4]

/euclid sd(3,8) '.  a      z . ' 
#endif


STATE_VERSION(1,  )

void init_state(void) {
}

float rompler(const char *fname) {
	wave_t *wave=get_wave_by_name(fname);
	return (wave && wave->frames) ? wave->frames[((G->sampleidx/2) % (wave->num_frames))*wave->channels] : 0.f;
}


stereo do_sample(stereo inp) {
    F chord1 = sawo(P_C3) + sawo(P_Ds4) + sawo(P_C4) + pwmo(P_C1,0.25) + sino(P_C5) + sino(P_C6);
    F chord2 = sawo(P_Gs2) + sawo(P_F4) + sawo(P_C4) + pwmo(P_F1,0.25) + sino(P_F5) + sino(P_Ds6);
    F chord3 = sawo(P_D3) + sawo(P_B3) + sawo(P_G2) + pwmo(P_G1,0.25) + sino(P_G5);
    //return stereo{sino(P_C5)};
    chord1 = chord1 * vol(slew(S0(0.))) + 
        chord2 * vol(slew(S1(0.))) +
        chord3 * vol(slew(S2(0.)));
   //F bass = sclip(lpf_dp(sawo(P_C1)+sawo(P_C2*1.03324f)+sawo(P_C2*0.99532f), P_C3, 0.1f))*0.2; // growly bass    
   F drums = sclip(rompler("break_sesame") * pow4(S4(0))*10.); 
   F dc = S5(0) * 2.;
   F mixed = chord1;
    stereo dry=stereo{mixed*0.5f,mixed*0.5f};
    stereo wet=reverb(dry*0.5f);
    wet = wet+dry*0.2+drums*0.5f;
    
    return wet + dc;
}
