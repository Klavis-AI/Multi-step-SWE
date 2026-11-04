# Build entry point for /app.
#
#   make            -> build/libsqlite3.a and build/sqlite3
#   make clean      -> remove build/
#
# Library translation units live in src/, the command-line shell's in shell/,
# and the public header in include/sqlite3.h.

CC       ?= cc
CFLAGS   ?= -O1 -w -std=c99
CPPFLAGS ?= -Iinclude -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_JSON1 -DHAVE_READLINE=0
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
