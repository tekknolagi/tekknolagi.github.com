OUT = bin
CFLAGS = -O0 -g -Wall -Wextra -pedantic -fno-strict-aliasing
TARGETS = mmap-demo compiling-integers compiling-immediates compiling-unary \
	  compiling-binary compiling-reader compiling-let compiling-if \
		compiling-heap live-programming
BINARIES = $(addprefix $(OUT)/, $(TARGETS))
TESTS = $(addprefix test-, $(TARGETS))

# $@ means the name of the target that caused the rule to run
# $^ means all of the prerequisites with spaces in between
# $< means the name of the first prerequisite

all: $(OUT) $(BINARIES)

test: $(OUT) $(TESTS)

$(OUT):
	mkdir -p $@

clean:
	rm $(OUT)/*

$(OUT)/%: %.c greatest.h
	$(CC) -std=c99 $(CFLAGS) $< -o $@

$(OUT)/%.o: %.c greatest.h
	$(CC) -std=c99 $(CFLAGS) $< -o $@ -c

IMGUI = imgui.cpp imgui_draw.cpp imgui_widgets.cpp imgui_impl_opengl3.cpp imgui_impl_sdl.cpp
PKGS = $(shell pkg-config --cflags sdl2)
LIBS = $(shell pkg-config --libs sdl2) $(shell pkg-config --libs glew)

$(OUT)/live-programming: $(OUT)/compiling-heap.o live-programming.cpp
	$(CXX) -std=c++11 $(CFLAGS) $(PKGS) $^ -o $@ $(IMGUI) $(LIBS)

test-%: $(OUT)/%
	./$<
