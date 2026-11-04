CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter
LDLIBS  := -lsecp256k1 -lcrypto -lm

# Vendored supranational/blst (Apache-2.0, impl/vendor/blst), built from source.
# src/server.c is blst's single-translation-unit build and build/assembly.S
# pulls in the ELF assembly for the host architecture.  __BLST_PORTABLE__
# compiles both the MULX/ADX and the baseline x86-64 paths and dispatches
# between them at run time, so the binary is not tied to the build host's CPU.
BLST_DIR    := vendor/blst
BLST_CFLAGS := -O2 -fno-builtin -Wall -Wextra -D__BLST_PORTABLE__
BLST_OBJ    := $(BLST_DIR)/src/server.o $(BLST_DIR)/build/assembly.o
BLST_DEP    := $(wildcard $(BLST_DIR)/src/*.c) $(wildcard $(BLST_DIR)/src/*.h) \
               $(wildcard $(BLST_DIR)/build/elf/*)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

evm: $(OBJ) $(BLST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(BLST_OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -Isrc -I$(BLST_DIR) -c -o $@ $<

$(BLST_DIR)/src/server.o: $(BLST_DEP)
	$(CC) $(BLST_CFLAGS) -c -o $@ $(BLST_DIR)/src/server.c

$(BLST_DIR)/build/assembly.o: $(BLST_DIR)/build/assembly.S $(BLST_DEP)
	$(CC) $(BLST_CFLAGS) -c -o $@ $(BLST_DIR)/build/assembly.S

clean:
	rm -f src/*.o $(BLST_OBJ) evm

.PHONY: clean
