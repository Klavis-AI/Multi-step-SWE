# Builds the Go toolchain into $(GOROOT)/bin/go.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wno-unused -Wno-misleading-indentation
LDFLAGS ?= -lm
GOROOT := $(CURDIR)/goroot

SRCS = src/util.c src/lex.c src/parse.c src/types.c src/constx.c src/check.c \
       src/runtime.c src/vm.c src/natives.c src/gocmd.c src/gotest.c src/vet.c
OBJS = $(SRCS:.c=.o)

all: $(GOROOT)/bin/go

$(GOROOT)/bin/go: $(OBJS)
	@mkdir -p $(GOROOT)/bin
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c src/go.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(GOROOT)/bin/go

.PHONY: all clean
