
#ifdef PATTERNS

/crunch misc:1? | - | shaker_large:4 
/rim AkaiXR10_sd:0 gain 0.3 sus 0 dec 0.2
/drum_pattern [ - hh:3 - hh:3 - hh:3 [ - hh:3?0.7 ^ 0.3 ] hh:3 ] gain 0.5 sus 0 dec 0.2
	// ,[/crunch]*16
	,[- - /crunch - /rim - rim:3 gain 0.4 subroc3d:7 dec 0.05-0.2 sus 0 ]
	//,[bd(3,16)/2:14]
	,[[snare_modern:17 from 0.7 to 0 gain 0.4]  snare_modern:17 : g3 gain 0.7]
    
/breakz [break_riffin*8] fitn 16
    	from [[0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 ]/2 round div 16]

/bassline <a1*2 a2  g1*6 g2*2 f1*2 f2*6 [e1 e1 ] [- [e2 e3 e1 e2]]> gain 1 : 1.3 sus 0 dec 0.2 : -0.5

/sub <<a1 g1 f1 e1> : -0.99 att 0.1 rel 0.3 gain 0.3>/2

/organ <[a3,e5,a5,e4] [a3,e5,g5,d4] [f3,c5,a5,f4] [e3,b4,b5,c4,e4]>/2
/plink  <[ - a2 a3 a2] [g g2] g f f2 [e e2] [c c2 b2 b]>



/cafe /organ gain 0.3 att 1.9 rel 0.9 : -1
	/plink clip 1 sus 0.5 dec 0.1 rel 0.2 add 36 pan 0-1 gain [0.1-0.25 | 0.2 | 0.4] : -1
    ,/sub

/recorder <[a2,c3,e3,a3,c4] [g2,d3,g3,b3] [f2,c3,f3,a3] [e2,e3,g3,b3]>/2 : recorder_tenor_vib att 0.5 rel 0.5 gain 0.4


/numbers <- - - num : 0-16> gain 0.5

/bass /bassline, /bassline vib 0.1 vibf 10

/bpm 130
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
struct song : public song_base_t {
	synth_t _pads, _drums, _bass, _vocals, _chords, _breakz;
    filter_t echofilt;
    ott_t ott;
    reverb_t reverb;
    delay_t delay0, delay1;

    stereo do_sample(stereo inp) {
        stereo rv = {};
        stereo drums = _drums("/drum_pattern");F dlvl=/*======*/cc(0);  // drums
        stereo breakz = _breakz("/breakz", 			/*======*/cc(1)); // breakbeat
        stereo bass = _bass("/bass", 					/*======*/cc(2)); // bass
        stereo chords = _chords("/recorder",			/*======*/cc(3)); // chords
        stereo vocals = _vocals("/numbers",			/*======*/cc(4)); // vocals
        
        // side-chain
        drums += breakz;
        float envf = envfollow(drums, 0.1f, 0.5);
        drums *= dlvl*dlvl;
        stereo pads = _pads("/cafe",              0.3*/*======*/cc(5)); // pads
        pads+=chords;
        preview *= 0.5 * 									/*========*/cc(6); // preview
        pads += delay0(pads + vocals*0.4, st(1.5,1.), 1., 1.2) * 0.5;
        bass += echofilt.hpf(delay1(bass, st(0.75, 1.5), 0.25f, 1.2f) * 0.25, 500.f);
        //pads+=vocals * 0.1;
        pads+=reverb(pads*0.5f + preview * 0.05f);
        pads/=envf;

        rv = drums + bass + pads + vocals * 2. + preview * 2.;
        preview*=0.;
        rv = ott(rv, /*======*/0.31);
        
        rv = rv  * 2. *			/*=========*/cc(7); // master volume
        // final vu meter for fun
        envfollow(rv.l);
        envfollow(rv.r);
        return rv;
    }
};

