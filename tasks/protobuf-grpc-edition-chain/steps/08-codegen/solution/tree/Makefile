# Build for the protobuf/gRPC implementation.
#
# `make` produces every program this project provides into bin/, named as the
# upstream interface names it. No third-party library is linked: the only
# external dependency is libc (and libm for the float formatting the text
# format needs).

CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -Wall -Wextra -Werror=implicit-function-declaration
CPPFLAGS += -Iinclude
LDLIBS  += -lm

SRC     := $(filter-out src/protoc_main.c src/conformance_main.c \
                        src/grpc_server_main.c src/grpc_client_main.c \
                        src/pygen_main.c \
                        src/hpack.c src/huff_table.c,$(wildcard src/*.c))
OBJ     := $(SRC:.c=.o)
# Every object depends on every header. Without this, adding a field to a
# struct recompiles only the .c files that changed and links them against
# objects still using the old layout -- which reads fields at the wrong offset
# and crashes far from the edit.
HDRS    := $(wildcard include/*.h)

PROGRAMS := bin/protoc bin/conformance bin/protoc-gen-py

all: $(PROGRAMS)

bin/protoc: $(OBJ) src/protoc_main.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/conformance: $(OBJ) src/conformance_main.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Links src/pygen_main.o alone: the plugin depends on libc only.
bin/protoc-gen-py: src/pygen_main.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^

bin/test_parse: $(OBJ) test/test_parse.o
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# descnum.h and wkt_data.c are generated from the bundled protoc release's
# descriptor.proto and well-known types. Regenerate when the pin moves.
regen:
	python3 tools/gen_descnum.py wkt/google/protobuf/descriptor.proto include/descnum.h
	python3 tools/embed_wkt.py wkt src/wkt_data.c

clean:
	rm -f src/*.o test/*.o
	rm -rf bin

.PHONY: all clean regen

# ---- gRPC over HTTP/2 (step 6).
# These are separate programs with their own main(), and SRC above excludes them
# so bin/protoc and bin/conformance keep linking exactly one main each. Under a
# wildcard Makefile a new source file is NOT additive: it joins every target
# that globs src/*.c unless explicitly filtered out.
GRPC_OBJ := src/hpack.o src/huff_table.o
bin/grpc_server: src/grpc_server_main.o $(GRPC_OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
bin/grpc_client: src/grpc_client_main.o $(GRPC_OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $^
grpc: bin/grpc_server bin/grpc_client
.PHONY: grpc
