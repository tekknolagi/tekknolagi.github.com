OUT = bin
CFLAGS = -O0 -g -Wall -Wextra -pedantic -fno-strict-aliasing -std=c99
TARGETS = mmap-demo compiling-integers compiling-immediates compiling-unary \
	  compiling-binary compiling-reader compiling-let compiling-if \
		compiling-heap compiling-procedures
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
	$(CC) $(CFLAGS) $< -o $@

test-%: $(OUT)/%
	./$<
