// enum, default, min, max
// the first 3 must be number, note, sound to match the value type enum
X(P_NUMBER, 0, -1000000, 1000000) // a number not yet assigned a meaning :)
X(P_NOTE, 0, 0, 127)
X(P_SOUND,0,0, 1000000) // an index into the sound map

X(P_DEGRADE, 0, 0, 1) // 
X(P_VARIANT,0,0, 1000000) // the : part of the sound
X(P_GLIDE, 0, 0, 1)
X(P_CUTOFF, 0, 0, 1)
X(P_RESONANCE, 0, 0, 1)
X(P_VELOCITY, 0, 0, 1)
X(P_A, 0.f, 0, 1)
X(P_D, 0.1f, 0, 1)
X(P_S, 1.f, 0, 1)
X(P_R, 0.f, 0, 1)
X(P_VOLUME, 1.f, 0, 1)
X(P_PAN, 0.f, -1.f, 1.f)
#undef X