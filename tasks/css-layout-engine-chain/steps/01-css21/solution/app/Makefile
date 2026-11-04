CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=
LDLIBS  ?= -lm

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/obj/%.o,$(SRCS))

all: build/cssdriver

build/cssdriver: $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/obj/%.o: src/%.c
	@mkdir -p build/obj
	$(CC) $(CFLAGS) -Isrc -c -o $@ $<

clean:
	rm -rf build

.PHONY: all clean
