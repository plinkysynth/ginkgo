# config: debug (default) or release
CONFIG ?= debug

# common flags
CXXFLAGS_COMMON  = -fPIC -g -std=c++11 -MMD -MP \
                   -I/opt/homebrew/opt/glfw/include -Isrc/ -I. \
                   -Wno-vla-cxx-extension -Werror -Wno-string-plus-int \
                   -fstandalone-debug -gdwarf-4 -fno-omit-frame-pointer

LDFLAGS_COMMON   = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
                   -framework Cocoa -framework IOKit -framework CoreVideo \
                   -framework OpenGL -framework CoreMIDI

# per-config bits
CXXFLAGS_debug   = -O0 # -fsanitize=address
LDFLAGS_debug    = # -fsanitize=address

CXXFLAGS_release = -O3
LDFLAGS_release  =

# final flags depend on CONFIG (evaluated later)
CXXFLAGS = $(CXXFLAGS_COMMON) $(CXXFLAGS_$(CONFIG))
LDFLAGS  = $(LDFLAGS_COMMON)  $(LDFLAGS_$(CONFIG))

LIB_NAME := ginkgo_lib.a
LIB_SRC  := src/ginkgo_lib.cpp src/miniparse.cpp src/utils.cpp
APP_SRC  := src/ginkgo.cpp src/sampler.cpp src/http_fetch.cpp

APP_OBJ := $(patsubst src/%.cpp,build/%.o,$(APP_SRC)) build/mac_touch_input.o
LIB_OBJ := $(patsubst src/%.cpp,build/%.o,$(LIB_SRC))
DEP     := $(APP_OBJ:.o=.d) $(LIB_OBJ:.o=.d) build/mac_touch_input.d

.PHONY: all debug release clean

all: ginkgo

debug:  CONFIG = debug
debug:  ginkgo

release: CONFIG = release
release: ginkgo

ginkgo: $(APP_OBJ) build/$(LIB_NAME)
	$(CXX) -o $@ $(APP_OBJ) build/$(LIB_NAME) $(LDFLAGS)

build/$(LIB_NAME): $(LIB_OBJ) | build
	ar rcs $@ $^

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/mac_touch_input.o: src/mac_touch_input.mm | build
	clang++ -ObjC++ $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

clean:
	rm -rf build ginkgo
