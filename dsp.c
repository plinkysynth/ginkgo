//   __ _ _   _  __| (_) ___
//  / _` | | | |/ _` | |/ _ \
// | (_| | |_| | (_| | | (_) |
//  \__,_|\__,_|\__,_|_|\___/
//                            

STATE_VERSION(1,
  float saw1;
  float saw2;
	float saw3;
)

stereo do_sample(state *G, stereo inp, uint32_t sampleidx) {
		float dt = 0.0132 / OVERSAMPLE;
    float osc1=sawo(G->saw1,dt);//sawo(G->saw1+0.0125,dt);
    G->saw1=fracf(G->saw1+dt);
    return (stereo){osc1*0.25, osc1*0.25};
}
