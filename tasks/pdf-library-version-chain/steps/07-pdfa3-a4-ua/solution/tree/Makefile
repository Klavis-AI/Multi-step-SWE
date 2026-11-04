# Builds the PDF utilities into build/ under their real program names.
#
#   make            -> build/pdfinfo build/pdftotext build/pdffonts build/qpdf ...
#   make clean
#
# The utilities link one static library built from src/*.c.

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
LDLIBS  += -lz -lm

BUILD   := build
LIBSRC  := $(wildcard src/*.c)
LIBOBJ  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIBSRC))
TOOLSRC := $(wildcard src/tools/*.c)
TOOLS   := $(patsubst src/tools/%.c,$(BUILD)/%,$(TOOLSRC))

.PHONY: all clean
all: $(TOOLS)

$(BUILD)/obj/%.o: src/%.c src/pdf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD)/libpdf.a: $(LIBOBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD)/%: src/tools/%.c $(BUILD)/libpdf.a src/pdf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(BUILD)/libpdf.a $(LDLIBS)

clean:
	rm -rf $(BUILD)
