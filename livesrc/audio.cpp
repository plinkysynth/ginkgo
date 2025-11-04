STATE_VERSION(1,  )

#ifdef PATTERNS

 // /simple_pattern <[c a f e] ^ 1 sus 0 dec 0.3 rel .3 att 0, [c5 c5 c5 c5] sus 0 dec 0.2>
 
/crunch misc:1? | - | shaker_large:4 
/rim AkaiXR10_sd:0 gain 1.2 sus 0 dec 0.2
/fdrum_pattern [- - /crunch - /rim - rim:3 gain 0.4 subroc3d:7 dec 0.05-0.2 sus 0 ], [ - hh - hh - hh hh? hh ], [/crunch]*16 ,
[bd - - - - - bd gain 0.3 - - bd - - - bd gain 0.4 - -]

/drum_pattern breaks152:1

/zdrum_pattern  break_think fit
/bassline <a1 g1 f1 e2>/2

/cafe <[a3,e5,a5,e4] [a3,e5,g5,d4] [f3,c5,a5,f4] [e3,b4,b5,c4,e4]>/2 gain 0.4 att 0.9 rel 0.9,
<a [ - a2 a3 a2] [g g2] g f f2 [e e2] [c c2 b2 b]> clip 0.5 sus 0.2 gain 1.5 dec 0.1 rel 0.2 add 12 // , /bassline gain 1

/bpm 152
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(stereo inp) {
	float s = 0.f;
    
    static float e = 0.f;
    static float fo = 0.f;
    static int prevt;
	int t = ((int)(G->t * 32) & 31) | 32;
    if (prevt!=t) {
        int b = ctz(t);
        float r = rnd01()*5.f;
        if (r<b && prevt!=t) e=(r*0.2f)+0.2f;
        prevt=t;
        fo=1.f-expf(-rnd01()*5.f-3.f);
    }
        
    e*=fo;
    
    s = rndt()*e;	
    s = sclip(s + lpf(lpf(s,F_E2, 40.f), F_E3, 20.f)*20.f);
    s=lpf(s, 3800, 1);
    //F drums = 1. * sclip(rompler("break_amen")*2); 
    stereo rv = mono2st(s * 0.);
    //return stereo{drums,drums};
	rv += sclip(synth("/cafe") * 0.1);
    //stereo mix = test_patterns("/cafe");
	rv+=reverb(rv*0.25f);
	rv += sclip(synth("/drum_pattern") * 0.5);
    
    //return mix+drums;
    return rv * 0.5;
}


