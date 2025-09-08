CFLAGS  = -g -std=c11 -O0 -MMD -MP -I/opt/homebrew/opt/glfw/include
LDFLAGS = -L/opt/homebrew/opt/glfw/lib -lglfw \
          -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL

SRC = gpu.c
OBJ = $(SRC:%.c=build/%.o)
DEP = $(OBJ:.o=.d)

gpu: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p $@

-include $(DEP)

.PHONY: clean
clean:
	rm -rf build gpu
