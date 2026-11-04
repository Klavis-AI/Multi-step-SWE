# Builds the monolith binary at bin/dendrite, the path Dendrite's build
# documentation produces (`go build -o bin/ ./cmd/...`).
CC      ?= cc
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra -pthread
LDFLAGS ?=
LDLIBS  ?= -lsqlite3 -lssl -lcrypto -lyaml -lcurl -lz -lpthread -lm

# -MMD -MP makes the compiler record which headers each object depends on, and
# `-include` feeds those records back in.  Without them a change to a shared
# header rebuilds only the files that were edited, and the result is a binary
# whose translation units disagree about a struct's layout or an enum's
# numbering -- which presents as nonsense behaviour rather than as a build
# error.
DEPFLAGS := -MMD -MP

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(SRC:.c=.d)

all: bin/dendrite

bin/dendrite: $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP) bin/dendrite

-include $(DEP)

.PHONY: all clean
