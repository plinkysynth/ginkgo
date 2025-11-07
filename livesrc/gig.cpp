
#ifdef PATTERNS

/*
HELLO LONDON OPENSLOTS
this is GINKGO - she is new and unfinished / buggy so 
gotta keep it simple and...

sorry if crash / glitch / etc

*/

/crunch misc:1? | - | shaker_large:4 
/rim AkaiXR10_sd:0 gain 0.3 sus 0 dec 0.2
/drum_pattern [ - hh:3 - hh:3 - hh:3 [ - hh:3?0.7 ] hh:3 ] gain 0.5 sus 0 dec 0.2
	// ,[/crunch]*16
	,[- - /crunch - /rim - rim:3 gain 0.4 subroc3d:7 dec 0.05-0.2 sus 0 ]
	,[bd:14]
	,[[snare_modern:17 from 0.7 to 0 gain 0.4]  snare_modern:17 : g3 gain 0.7]
    
/breakz [break_riffin*8] fitn 16
    	from [[0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 ]/2 round div 16]

/bassline <a1*2 a2  g1*6 g2*2 f1*2 f2*6 [e1 e1 ] [- [e2 e3 e1 e2]]> gain 1 : 1.3 sus 0 dec 0.2 : -0.5

/sub <<a1 g1 f1 e1> : -0.99 att 0.1 rel 0.3 gain 0.3>/2

/organ <[a3,e5,a5,e4] [a3,e5,g5,d4] [f3,c5,a5,f4] [e3,b4,b5,c4,e4]>/2
/plink <a [ - a2 a3 a2] [g g2] g f f2 [e e2] [c c2 b2 b]>



/cafe /organ gain 0.7 att 1.9 rel 0.9 : -1
	/plink clip 1 sus 0.5 dec 0.1 rel 0.2 add 36 pan 0-1 gain [0.2-0.5 | 0.4 | 0.8] : -1,
    /sub

/recorder <[a2,c3,e3,a3,c4] [g2,d3,g3,b3] [f2,c3,f3,a3] [e2,e3,g3,b3]>/2 : recorder_tenor_vib att 0.5 rel 0.5 gain 0.4


/numbers <- - - num : 0-16> gain 0.5

/bass /bassline, /bassline vib 0.1 vibf 10

/bpm 130
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
STATE_VERSION(1,  
	synth_state_t pads, drums, bass, vocals, chords, breakz;
    float lpf1[2], lpf0[2],lpf2[2],hpf[4];
    stereo ottstate[16];
    reverb_state_t R;
    delay_state_t delay0, delay1;
)

stereo lol_ott(stereo rv, float amount);

// all the DSP and signal routing is just C :) lol.
stereo do_sample(stereo inp) {
    stereo rv = {};
    stereo drums = synth(&G->drums, "/drum_pattern");F dlvl=/*======*/cc(0);  // drums
    stereo breakz = synth(&G->breakz, "/breakz", 			/*======*/cc(1)); // breakbeat
    stereo bass = synth(&G->bass, "/bass", 					/*======*/cc(2)); // bass
    stereo chords = synth(&G->chords, "/recorder",			/*======*/cc(3)); // chords
    stereo vocals = synth(&G->vocals, "/numbers",			/*======*/cc(4)); // vocals
    
    // side-chain
    drums += breakz;
    float envf = envfollow(drums, 0.1f, 0.5);
    drums *= dlvl*dlvl;
    stereo pads = synth(&G->pads, "/cafe",              0.3*/*======*/cc(5)); // pads
    pads+=chords;
    G->preview *= 0.5 * 									/*========*/cc(6); // preview
    pads += delay(&G->delay0, pads + vocals*0.4, st(1.5,1.), 1., 1.2) * 0.5;
    bass += hpf(G->hpf, delay(&G->delay1, bass, st(0.75, 1.5), 0.25f, 1.2f) * 0.25, 500.f);
    //pads+=vocals * 0.1;
  	pads+=reverb(&G->R, pads*0.5f + G->preview * 0.05f);
    //drums3+=reverb(&G->R, drums*0.2f);
    pads/=envf;
    //bass/=(envf - 0.1) * 8.;



	rv = drums + bass + pads + vocals * 2. + G->preview * 2.;
    G->preview*=0.;
	rv = lol_ott(rv, /*======*/0.);
    
    rv = rv  *				/*=========*/cc(7); // master volume
    // final vu meter for fun
    envfollow(rv.l);
    envfollow(rv.r);
    return rv;
}

// inf upward, 4:1 downward
inline float compgain(float inp, float lothresh, float hithresh, float makeup) {
	if (inp > hithresh) return min(32.f,sqrtf(sqrtf(hithresh/inp)) * makeup);
    if (inp < lothresh) return min(32.f,lothresh/inp * makeup);
    return makeup;
}

stereo lol_ott(stereo rv, float amount) {
    stereo bands[3];
    // +5.2 on input then
    const float inp_gain = db2lin(12.2);
    multiband_split(rv * inp_gain, bands, G->ottstate);
    // thresh, decay, attack
    float b=envfollow(bands[0],0.02f, 0.01f, 0.001f);
    float m=envfollow(bands[1],0.02f, 0.01f, 0.001f);
    float h=envfollow(bands[2],0.02f, 0.01f, 0.001f);
    // -40.8 : -35.5 for hi,  inf:1 / 4:1
    // -41.8 : -30.2 for mid 66 : 1 / 4:1
    // -40.8 : -33.8 for lo  66 : 1 / 4:1
    // +10.3 out for lo/hi, +5.7 out for mid
    const float lothreshb = db2lin(-40.8),hithreshb=db2lin(-35.5);
    const float lothreshm = db2lin(-41.8),hithreshm=db2lin(-30.2);
    const float lothreshh = db2lin(-40.8),hithreshh=db2lin(-33.8);
    const float makeupb = db2lin(10.3);
    const float makeupm = db2lin(5.7);
    const float makeuph = db2lin(8.3);
    float bgain = lerp(1.f/inp_gain, compgain(b,lothreshb,hithreshb, makeupb), amount);
    float mgain = lerp(1.f/inp_gain, compgain(m,lothreshm,hithreshm, makeupm), amount);
    float hgain = lerp(1.f/inp_gain, compgain(h,lothreshh,hithreshh, makeuph), amount);
    vu(b*bgain, lothreshb * makeupb) ;
    vu(m*mgain, lothreshm * makeupm);
    vu(h*hgain, lothreshh * makeuph);
    rv=sclip(bands[0] * bgain + bands[1] * mgain + bands[2] * hgain);
    return rv;
}

