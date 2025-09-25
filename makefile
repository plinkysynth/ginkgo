CFLAGS  = -g -std=c11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include -Isrc/ -I.
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreMIDI

SRC = src/ginkgo.c
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
DEP = $(OBJ:.o=.d)

ginkgo: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build ginkgo
