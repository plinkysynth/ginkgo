// enum, shortname, default, min, max
// the first 3 must be number, note, sound to match the value type enum
X(P_NUMBER, ":",0, -1000000, 1000000) // a number not yet assigned a meaning :)
X(P_NOTE, "note", 0, 0, 127)
X(P_SOUND,"s", 0,0, 1000000) // an index into the sound map

X(P_GLIDE, "gli", 0, 0, 1)
X(P_CUTOFF, "cut", 0, 0, 1)
X(P_RESONANCE, "res", 0, 0, 1)
X(P_VELOCITY, "vel", 0, 0, 1)
X(P_A, "att", 0.f, 0, 1)
X(P_D, "dec", 0.1f, 0, 1)
X(P_S, "sus", 1.f, 0, 1)
X(P_R, "rel", 0.f, 0, 1)
X(P_VOLUME, "vol", 1.f, 0, 1)
X(P_PAN, "pan", 0.f, -1.f, 1.f)
#undef X