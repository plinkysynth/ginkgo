CFLAGS  = -fPIC -g -std=c++11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include -Isrc/ -I. -Wno-vla-cxx-extension # -fsanitize=address
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL -framework CoreMIDI # -fsanitize=address

LIB_NAME := ginkgo_lib.a                         # static lib on macOS
LIB_SRC  := src/ginkgo_lib.cpp src/miniparse.cpp src/utils.cpp
APP_SRC  := src/ginkgo.cpp src/sampler.cpp src/http_fetch.cpp

APP_OBJ := $(patsubst src/%.cpp,build/%.o,$(APP_SRC))
LIB_OBJ := $(patsubst src/%.cpp,build/%.o,$(LIB_SRC))
DEP     := $(APP_OBJ:.o=.d) $(LIB_OBJ:.o=.d)

ginkgo: $(APP_OBJ) build/$(LIB_NAME)
	$(CC) -o $@ $(APP_OBJ) build/$(LIB_NAME) $(LDFLAGS)

build/$(LIB_NAME): $(LIB_OBJ) | build
	ar rcs $@ $^

build/%.o: src/%.cpp | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build ginkgo
