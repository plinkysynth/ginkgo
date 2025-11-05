
#ifdef PATTERNS


/crunch misc:1? | - | shaker_large:4 
/rim AkaiXR10_sd:0 gain 0.3 sus 0 dec 0.2
/drum_pattern [ - hh:3 - hh:3 - hh:3 [ - hh:3?0.7 ] hh:3 ] gain 0.5 sus 0 dec 0.2
	// ,[/crunch]*16
	// ,[- - /crunch - /rim - rim:3 gain 0.4 subroc3d:7 dec 0.05-0.2 sus 0 ]
	//,[bd:14 - - - - bd:14 - -]
	,[[snare_modern:17 from 0.7 to 0 gain 0.4]  snare_modern:17 : g3 gain 0.7]
	, break_riffin/2 fit gain [0.7 mul cc1]

/bassline <a1*2 a2*2 g1*6 g2*4 f1*2 f2*6 [e1 e1 ] [- [e2 e3 e1 e2]]> gain 1 : 1.3 sus 0 dec 0.2 : -0.5
/sub <<a1 g1 f1 e1> : -0.9 rel 0.3 gain 0.6>/2
/cafe2 <[a3,e5,a5,e4] [a3,e5,g5,d4] [f3,c5,a5,f4] [e3,b4,b5,c4,e4]>/2 gain 0.6 att 1.9 rel 0.9 : -1 //: fmpiano
	<a [ - a2 a3 a2] [g g2] g f f2 [e e2] [c c2 b2 b]> clip 1 sus 0.5 dec 0.1 rel 0.2 add 36 pan 0-1 gain [0.2-0.5 | 0.4 | 0.8] : -1,

/chord <[a2,c3,e3,a3,c4] [g2,d3,g3,b3] [f2,c3,f3,a3] [e2,e3,g3,b3]>/2 : recorder_tenor_vib att 0.5 rel 0.5

/cafe /cafe2,/sub

/numbers <- - - num : 0-16> gain 0.5

/bass /bassline, /bassline vib 0.1 vibf 10

/bpm 130
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
STATE_VERSION(1,  
	synth_state_t pads, drums, bass, vocals, chords;
    float lpf1[2], lpf0[2],lpf2[2];
)

stereo do_sample(stereo inp) {
    stereo rv = {};
    stereo drums = sclip(synth(&G->drums, "/drum_pattern"));
    stereo bass = sclip(synth(&G->bass, "/bass"));
    stereo chords = sclip(synth(&G->chords, "/chord"));
    stereo vocals = synth(&G->vocals, "/numbers");
    float envf = envfollow(drums, 0.25f, 0.5);
    stereo pads = synth(&G->pads, "/cafe") * 0.1;
    pads *= 								/*========*/cc(3); // pads
    pads += chords *                        /*========*/cc(4); // chords
    vocals *= 								/*========*/cc(5); // vocals
    G->preview *= 0.5 * 					/*========*/cc(6); // preview
    pads+=vocals;
  	pads+=reverb(pads*0.2f + G->preview * 0.1f);
    pads/=envf - 0.1;
    //bass/=(envf - 0.1) * 8.;

    rv  = drums *         /*========*/cc(0); // drums
                          /*========*/cc(1); // breakbeat
    rv += sclip(bass) *   /*========*/cc(2); // bass
    rv += sclip(pads);
    
    rv = rv * /*=========*/cc(7); // master volume
    // final vu meter for fun
    envfollow(rv.l);
    envfollow(rv.r);
    return rv * 0.5;
}




