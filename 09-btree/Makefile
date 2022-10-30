.PHONY: build test

SRC_SOLUTION := $(wildcard *.c)
HDR_SOLUTION := $(wildcard *.h)

SRC_STDLIB := $(wildcard ../stdlib/*.c)
HDR_SOLUTION := $(wildcard ../stdlib/*.h)

test: build
	./a.out

build: a.out

a.out: $(SRC_SOLUTION) $(HDR_SOLUTION) $(SRC_STDLIB) $(HDR_STDLIB)
	gcc \
		-std=gnu11 -Wall -Wextra -Werror \
		-I. -I../stdlib \
		-D_GNU_SOURCE \
		-pthread \
		-g -Og \
		$(SRC_SOLUTION) $(SRC_STDLIB)
