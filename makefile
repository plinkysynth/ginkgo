CFLAGS  = -g -std=c11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include -Isrc/ -I.
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreMIDI

SRC = src/ginkgo.c src/miniparse.c src/sampler.c src/utils.c src/http_fetch.c
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
DEP = $(OBJ:.o=.d)
DLL = build/dsp.so

ginkgo: $(OBJ) $(DLL)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

# make the audio as the live update does, just to see if it works.
# the runtime actually does this on every boot, so its not necessary to do it here.
# but it adds error messages at build time, so its nice to see it working.
build/dsp.so: src/audio_stub.c | build
	clang -x c -g -std=c11 -O2 -fPIC -dynamiclib -fno-caret-diagnostics -fno-color-diagnostics -D LIVECODE -I. -Isrc/ src/audio_stub.c -o build/dsp.so

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build ginkgo
