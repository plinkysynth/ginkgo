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
	static float lpf = 0.f;

    float dt = 0.00287235 / 8. * (2.f+G->cc[16]*6.);
    float osc1=sawo(G->saw1,dt);//sawo(G->saw1+0.0125,dt);
lpf+=(osc1-lpf) * squaref(squaref(G->cc[17]));
    G->saw1=fracf(G->saw1+dt);
    return (stereo){lpf*0.25, lpf*0.25};
}
