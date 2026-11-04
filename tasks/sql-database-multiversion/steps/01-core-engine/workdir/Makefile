# Build entry point for /app.
#
#   make            -> build/libsqlite3.a and build/sqlite3
#   make clean      -> remove build/
#
# The verifier runs `make` in /app with no arguments and then uses
# build/libsqlite3.a, build/sqlite3 and include/sqlite3.h. See
# TEST_INTERFACE.md section 1 for the contract this file must keep. You may
# rewrite this file however you like as long as it keeps producing those
# outputs from the sources in the workspace, offline.

CC       ?= cc
CFLAGS   ?= -O1 -std=c99 -Wall
CPPFLAGS ?= -Iinclude
LDLIBS   ?= -lm -lpthread -ldl

LIB_SOURCES   := $(sort $(wildcard src/*.c))
SHELL_SOURCES := $(sort $(wildcard shell/*.c))
LIB_OBJECTS   := $(patsubst src/%.c,build/obj/src/%.o,$(LIB_SOURCES))
SHELL_OBJECTS := $(patsubst shell/%.c,build/obj/shell/%.o,$(SHELL_SOURCES))

.PHONY: all clean
all: build/libsqlite3.a build/sqlite3

build/obj/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/obj/shell/%.o: shell/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/libsqlite3.a: $(LIB_OBJECTS)
	@mkdir -p build
	$(AR) rcs $@ $(LIB_OBJECTS)

build/sqlite3: $(SHELL_OBJECTS) build/libsqlite3.a
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHELL_OBJECTS) build/libsqlite3.a -o $@ $(LDLIBS)

clean:
	rm -rf build
