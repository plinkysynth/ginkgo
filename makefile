CXX := clang++
CC := clang
UNAME_S := $(shell uname -s)

# config: debug (default) or release
CONFIG ?= debug

CXXFLAGS_COMMON = -g -std=c++11 -MMD -MP -Isrc/ -I. \
	-Wno-vla-cxx-extension -Werror -Wno-string-plus-int -Wno-reorder-init-list -Wno-c99-designator \
	-fstandalone-debug -fno-omit-frame-pointer

ifeq ($(UNAME_S),Darwin)
#################################### MACOS ####################################
TARGET      = ginkgo_mac
LIB_NAME    := ginkgo_lib.a
BUILDDIR    = build/mac

CXXFLAGS_COMMON += -fPIC -I/opt/homebrew/opt/glfw/include
APP_EXTRA_OBJ   = $(BUILDDIR)/mac_touch_input.o
LDFLAGS_COMMON  = -L/opt/homebrew/opt/glfw/lib -lglfw -lcurl \
                  -framework Cocoa -framework IOKit -framework CoreVideo \
                  -framework OpenGL -framework CoreMIDI

# --- app bundle + icon bits ---

APP_NAME      := ginkgo
APP_BUNDLE    := build/$(APP_NAME).app
APP_MACOS_DIR := $(APP_BUNDLE)/Contents/MacOS
APP_RES_DIR   := $(APP_BUNDLE)/Contents/Resources

ICON_NAME   := ginkgo_icon
ICON_PNG    := assets/$(ICON_NAME).png
ICONSET_DIR := build/$(ICON_NAME).iconset
ICON_ICNS   := build/$(ICON_NAME).icns

PLIST_SRC := assets/Info.plist
PLIST_DST := $(APP_BUNDLE)/Contents/Info.plist

# make the default 'all' also produce the .app bundle on macOS
# all: $(APP_BUNDLE)

else ifeq ($(UNAME_S),Linux)
#################################### LINUX ####################################
TARGET      = ginkgo_linux
LIB_NAME    := ginkgo_lib.a
BUILDDIR    = build/linux
CXXFLAGS_COMMON += -fPIC -D__LINUX__
APP_EXTRA_OBJ   =
else ifeq ($(OS),Windows_NT)
#################################### WINDOWS ####################################
TARGET      = ginkgo_windows
LIB_NAME    := ginkgo_lib.lib
BUILDDIR    = build/windows
CXXFLAGS_COMMON += -D__WINDOWS__
LDFLAGS_COMMON = -lglfw3 -lcurl -lopengl32 -lws2_32 -lwinmm -lgdi32
APP_EXTRA_OBJ   =
else
$(error Unsupported platform)
endif

# per-config bits
CXXFLAGS_debug  = -O0 -fsanitize=address
LDFLAGS_debug   = -fsanitize=address
CXXFLAGS_release = -O3
LDFLAGS_release  =

# final flags depend on CONFIG (evaluated later)
CXXFLAGS = $(CXXFLAGS_COMMON) $(CXXFLAGS_$(CONFIG))
LDFLAGS  = $(LDFLAGS_COMMON)  $(LDFLAGS_$(CONFIG))

LIB_SRC := src/ginkgo_lib.cpp src/miniparse.cpp src/utils.cpp
APP_SRC := src/ginkgo.cpp src/sampler.cpp src/http_fetch.cpp

APP_OBJ := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(APP_SRC)) $(APP_EXTRA_OBJ)
LIB_OBJ := $(patsubst src/%.cpp,$(BUILDDIR)/%.o,$(LIB_SRC))

DEP := $(APP_OBJ:.o=.d) $(LIB_OBJ:.o=.d) $(BUILDDIR)/mac_touch_input.d

.PHONY: all debug release clean

all: $(TARGET)

debug: CONFIG = debug
debug: $(TARGET)

release: CONFIG = release
release: $(TARGET)

# ----------------------------
# Link the main binary (all OS)
# ----------------------------
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
	rm -rf $(BUILDDIR) $(TARGET) build/ginkgo.app build/ginkgo_icon.iconset build/ginkgo_icon.icns

# ------------------------------------------------------------------
# macOS-only rules to build the .icns and bundle in build/ginkgo.app
# ------------------------------------------------------------------
ifeq ($(UNAME_S),Darwin)

# build/ginkgo_icon.icns from assets/ginkgo_icon.png
$(ICON_ICNS): $(ICON_PNG)
	rm -rf $(ICONSET_DIR)
	mkdir -p $(ICONSET_DIR)
	sips -s format png $< --out $(ICONSET_DIR)/icon_512x512.png
	iconutil -c icns $(ICONSET_DIR) -o $@

# bundle: build/ginkgo.app
$(APP_BUNDLE): $(TARGET) $(ICON_ICNS) $(PLIST_SRC)
	mkdir -p $(APP_MACOS_DIR) $(APP_RES_DIR)
	cp $(TARGET) $(APP_MACOS_DIR)/$(APP_NAME)
	cp $(ICON_ICNS) $(APP_RES_DIR)/$(ICON_NAME).icns
	cp $(PLIST_SRC) $(PLIST_DST)
	cp -R assets $(APP_RES_DIR)/assets
	cp -R livesrc $(APP_RES_DIR)/livesrc
	cp -R src $(APP_RES_DIR)/src
	mkdir -p $(APP_RES_DIR)/build/mac
	mkdir -p $(APP_RES_DIR)/3rdparty
	cp 3rdparty/stb*.h $(APP_RES_DIR)/3rdparty
	cp -R build/mac/ginkgo_lib.a $(APP_RES_DIR)/build/mac

endif
