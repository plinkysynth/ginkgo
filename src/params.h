// enum, shortname, default, min, max
// the first 3 must be number, note, sound to match the value type enum
X(P_NUMBER, ":",0, -1000000, 1000000) // a number not yet assigned a meaning :)
X(P_NOTE, "note", 3*12, 0, 127)
X(P_SOUND,"s", 0,0, 1000000) // an index into the sound map
X(P_GATE, "^", 0.75f, 0, 1)
X(P_CUTOFF, "cut", 0, 0, 1)
X(P_RESONANCE, "res", 0, 0, 1)
X(P_A, "att", 0.f, 0.01, 1)
X(P_D, "dec", 0.3f, 0, 1)
X(P_S, "sus", 0.5f, 0, 1)
X(P_R, "rel", 0.0f, 0, 1)
X(P_GAIN, "gain", 0.75f, 0, 1)
X(P_PAN, "pan", 0.5f, 0.f, 1.f)
X(P_LOOPS, "loops", 0.f, 0.f, 1.f)
X(P_LOOPE, "loope", 0.f, 0.f, 1.f)
X(P_SCALEBITS, "scale", 0.f, 0.f, (float)(1<<24))
#undef X
