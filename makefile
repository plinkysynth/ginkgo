CXX := clang++
CC := clang
UNAME_S := $(shell uname -s)
# config: debug (default) or release
CONFIG ?= debug
CXXFLAGS_COMMON  = -g -std=c++11 -MMD -MP -Isrc/ -I. \
	-Wno-vla-cxx-extension -Werror -Wno-string-plus-int -Wno-reorder-init-list -Wno-c99-designator \
	-fstandalone-debug -fno-omit-frame-pointer 

ifeq ($(UNAME_S),Darwin)
#################################### MACOS ####################################
TARGET = ginkgo_mac
LIB_NAME := ginkgo_lib.a
BUILDDIR = build/mac
CXXFLAGS_COMMON += -fPIC  -I/opt/homebrew/opt/glfw/include
                   
APP_EXTRA_OBJ = $(BUILDDIR)/mac_touch_input.o
LDFLAGS_COMMON   = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
                   -framework Cocoa -framework IOKit -framework CoreVideo \
                   -framework OpenGL -framework CoreMIDI
else ifeq ($(UNAME_S),Linux)
#################################### LINUX ####################################
TARGET = ginkgo_linux
LIB_NAME := ginkgo_lib.a
BUILDDIR = build/linux
CXXFLAGS_COMMON += -fPIC -D__LINUX__
APP_EXTRA_OBJ = 
else ifeq ($(OS),Windows_NT)
#################################### WINDOWS ####################################
TARGET = ginkgo_windows
LIB_NAME := ginkgo_lib.lib
BUILDDIR = build/windows
CXXFLAGS_COMMON += -D__WINDOWS__
APP_EXTRA_OBJ = 
else

    $(error Unsupported platform)
endif

# per-config bits
CXXFLAGS_debug   = -O0 # -fsanitize=address
LDFLAGS_debug    = # -fsanitize=address

CXXFLAGS_release = -O3
LDFLAGS_release  =

# final flags depend on CONFIG (evaluated later)
CXXFLAGS = $(CXXFLAGS_COMMON) $(CXXFLAGS_$(CONFIG))
LDFLAGS  = $(LDFLAGS_COMMON)  $(LDFLAGS_$(CONFIG))

LIB_SRC  := src/ginkgo_lib.cpp src/miniparse.cpp src/utils.cpp
APP_SRC  := src/ginkgo.cpp src/sampler.cpp src/http_fetch.cpp

APP_OBJ := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(APP_SRC)) $(APP_EXTRA_OBJ)
LIB_OBJ := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRC))
DEP     := $(APP_OBJ:.o=.d) $(LIB_OBJ:.o=.d) $(BUILDDIR)/mac_touch_input.d

.PHONY: all debug release clean

all: $(TARGET)

debug:  CONFIG = debug
debug:  $(TARGET)

release: CONFIG = release
release: $(TARGET)

$(TARGET): $(APP_OBJ) $(BUILDDIR)/$(LIB_NAME)
	$(CXX) -o $@ $(APP_OBJ) $(BUILDDIR)/$(LIB_NAME) $(LDFLAGS)

$(BUILDDIR)/$(LIB_NAME): $(LIB_OBJ) | $(BUILDDIR)
	ar rcs $@ $^

$(BUILDDIR)/%.o: src/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/mac_touch_input.o: src/mac_touch_input.mm | $(BUILDDIR)
	clang++ -ObjC++ $(CXXFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $@

-include $(DEP)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
