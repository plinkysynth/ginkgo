#include "ginkgo.h"

typedef struct state {
    int saw;
} state;

////////////////////////////////////////////////////////////////////////////////////
//                  _ _       
//   __ _ _   _  __| (_) ___  
//  / _` | | | |/ _` | |/ _ \ 
// | (_| | |_| | (_| | | (_) |
//  \__,_|\__,_|\__,_|_|\___/ 
//                            
////////////////////////////////////////////////////////////////////////////////////

stereo do_sample(state *G, stereo inp) {
    float saw=sinf(G->saw++ * 0.03253f) * 1.f;
    return (stereo){saw,saw};
}


////////////////////////////////////////////////////////////////////////////////////
EXPORT void *dsp(void *G, stereo *audio, int frames, int reloaded) {
    if (!G) G = calloc(1, sizeof(state));
    for (int i = 0; i < frames; i++) 
        audio[i] = do_sample((state*)G, audio[i]);
    return G;
}
