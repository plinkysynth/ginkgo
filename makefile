CFLAGS  = -fPIC -g -std=c++11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include -Isrc/ -I. -Wno-vla-cxx-extension # -fsanitize=address
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreMIDI # -fsanitize=address

SRC = src/ginkgo.cpp src/miniparse.cpp src/sampler.cpp src/utils.cpp src/http_fetch.cpp src/ginkgo_lib.cpp
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC))
DEP = $(OBJ:.o=.d)

ginkgo: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build ginkgo
