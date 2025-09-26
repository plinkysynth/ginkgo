// TODO - this assumes that its livesrc/audio.c that we want
// the runtime builds these two include lines by hand and feeds them to clang via stdin.
// clang -x c -g -std=c11 -O2 -fPIC -dynamiclib -fno-caret-diagnostics -fno-color-diagnostics -D LIVECODE -I. -Isrc/ src/audio_stub.c -o build/dsp.so
#include "ginkgo.h"
#include "livesrc/audio.c"
