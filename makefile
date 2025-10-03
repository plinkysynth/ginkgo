CFLAGS  = -fPIC -g -std=c11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include -Isrc/ -I. # -fsanitize=address
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreMIDI # -fsanitize=address

SRC = src/ginkgo.c src/miniparse.c src/sampler.c src/utils.c src/http_fetch.c src/ginkgo_lib.c
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
DEP = $(OBJ:.o=.d)
DLL = build/dsp.so

ginkgo: $(OBJ) $(DLL)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build ginkgo
