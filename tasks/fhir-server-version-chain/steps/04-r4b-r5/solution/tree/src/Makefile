CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -pthread
LDFLAGS ?= -pthread
LDLIBS  ?= -lm

SRC := $(wildcard *.c)
OBJ := $(patsubst %.c,../build/%.o,$(SRC))
HDR := $(wildcard *.h) $(wildcard *.inc)

all: ../bin/blaze

../bin/blaze: $(OBJ)
	@mkdir -p ../bin
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

../build/%.o: %.c $(HDR)
	@mkdir -p ../build
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf ../build ../bin

.PHONY: all clean
