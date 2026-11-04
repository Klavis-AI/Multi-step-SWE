# ---------------------------------------------------------------------------
# Makefile -- version-agnostic, JIT-free, three-width PCRE2 test build.
#
# Validated unmodified against pcre2-10.20, 10.22, 10.23, 10.30, 10.32, 10.33,
# 10.34, 10.39, 10.42, 10.43, 10.45, 10.47 and 10.48.
#
#   make -j12          # -> build/pcre2test
#   make runtest       # run the tree's own RunTest against it
#   make srclist       # show discovered / excluded sources
#   make clean         # remove build/
#   make distclean     # also un-stage generated files
#
# Run from the root of a PCRE2 source checkout (the directory holding src/).
#
# Source discovery is a GLOB over src/pcre2_*.c minus two exclusion sets, so a
# candidate who adds src/pcre2_foo.c gets it compiled with no edit here:
#
#   1. Tool translation units that are not part of the library.
#   2. Auto-detected fragments: any pcre2_*.c that another *library* source
#      #includes (pcre2_jit_match.c, pcre2_jit_misc.c, pcre2_printint.c,
#      pcre2_ucptables.c, ...). These are #included, never compiled alone.
#      Detection deliberately scans only library candidates, never the tools --
#      pcre2test.c and pcre2_dftables.c #include real TUs (pcre2_tables.c,
#      pcre2_ucd.c, pcre2_valid_utf.c, pcre2_maketables.c) that MUST be built.
#
# This reproduces each tag's own Makefile.am COMMON_SOURCES exactly, with no
# hardcoded per-version file lists.
#
# JIT is off: SUPPORT_JIT is never defined, so pcre2_jit_compile.c compiles to
# a stub and deps/sljit (a submodule from 10.45 on) is never referenced.
# ---------------------------------------------------------------------------

SHELL  := /bin/sh
.SUFFIXES:

SRCDIR := src
BUILD  := build
OBJDIR := $(BUILD)/obj

CC     ?= cc
AR     ?= ar
OPT    ?= -O2
WARN   ?= -w

# Config macro set proven across 10.20..10.48.
#  - Booleans appear as "/* #undef X */" in config.h.generic, so -D works
#    without editing the header.
#  - HAVE_STRERROR is mandatory at 10.23..10.45: without it pcre2test.c
#    compiles a strerror() fallback over sys_nerr/sys_errlist, which glibc
#    2.36 (bookworm) removed, and the link fails.
CFG := -DHAVE_CONFIG_H -DPCRE2_STATIC -DSUPPORT_UNICODE \
       -DSUPPORT_PCRE2_8 -DSUPPORT_PCRE2_16 -DSUPPORT_PCRE2_32 \
       -DHAVE_STRERROR=1 -DHAVE_UNISTD_H=1 -DHAVE_MEMMOVE=1

CFLAGS   ?= $(OPT) $(WARN)
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=
INCS     := -I$(SRCDIR) -I.

# Force config.h in at the start of every TU, as upstream's own config.h.generic
# instructs. Needed because the convention moved: up to ~10.43 each src/*.c
# carries its own "#ifdef HAVE_CONFIG_H / #include \"config.h\"" preamble, while
# from 10.45 on pcre2_internal.h does it. Force-including makes a
# candidate-authored engine source build under either convention. It is
# idempotent -- config.h holds only comments, #define, and #ifndef-guarded
# numerics -- and -D flags are processed before -include, so CFG still wins.
FORCE_CFG := -include $(SRCDIR)/config.h
COMPILE    = $(CC) $(CFLAGS) $(CFG) $(FORCE_CFG) $(INCS) $(CPPFLAGS)

# ---- generated files: staged from upstream templates, never clobbered ------
GEN := $(SRCDIR)/pcre2.h $(SRCDIR)/config.h $(SRCDIR)/pcre2_chartables.c

# ---- source discovery ------------------------------------------------------
# Tools: not library objects. (Most do not match pcre2_*.c anyway; listed for
# safety so the glob can be widened without surprises.)
TOOLS := pcre2_dftables.c pcre2_fuzzsupport.c pcre2_jit_test.c \
         pcre2test.c pcre2grep.c pcre2posix.c pcre2posix_test.c pcre2demo.c

CAND := $(filter-out $(TOOLS),$(notdir $(wildcard $(SRCDIR)/pcre2_*.c)))

INCLUDED := $(if $(CAND),$(shell cd $(SRCDIR) && \
    grep -hoE '^[ \t]*#[ \t]*include[ \t]*"pcre2_[a-z0-9_]+\.c"' $(CAND) 2>/dev/null | \
    grep -oE 'pcre2_[a-z0-9_]+\.c' | sort -u))

# EXTRA_EXCLUDE=... on the command line adds further exclusions.
EXCLUDE := $(sort $(TOOLS) $(INCLUDED) $(EXTRA_EXCLUDE))

# pcre2_chartables.c is generated, so it may not exist when the glob runs;
# add it unconditionally (sort dedupes if it is already present).
NAMES := $(sort $(filter-out $(EXCLUDE),$(CAND)) pcre2_chartables.c)

OBJ_8  := $(NAMES:%.c=$(OBJDIR)/%.8.o)
OBJ_16 := $(NAMES:%.c=$(OBJDIR)/%.16.o)
OBJ_32 := $(NAMES:%.c=$(OBJDIR)/%.32.o)

LIB8      := $(BUILD)/libpcre2-8.a
LIB16     := $(BUILD)/libpcre2-16.a
LIB32     := $(BUILD)/libpcre2-32.a
POSIX_SRC := $(wildcard $(SRCDIR)/pcre2posix.c)
LIBPOSIX  := $(if $(POSIX_SRC),$(BUILD)/libpcre2-posix.a,)
LIBS      := $(LIBPOSIX) $(LIB8) $(LIB16) $(LIB32)
TEST      := $(BUILD)/pcre2test

# ---------------------------------------------------------------------------
.PHONY: all clean distclean runtest srclist
all: $(TEST)

$(BUILD) $(OBJDIR):
	@mkdir -p $@

# No prerequisites: these fire only when the target is absent, so a file the
# candidate wrote themselves is never overwritten.
$(SRCDIR)/pcre2.h:
	@test -f $(SRCDIR)/pcre2.h.generic || { echo "ERROR: no $(SRCDIR)/pcre2.h.generic"; exit 1; }
	@cp $(SRCDIR)/pcre2.h.generic $@ && echo "  STAGE $@"

$(SRCDIR)/config.h:
	@test -f $(SRCDIR)/config.h.generic || { echo "ERROR: no $(SRCDIR)/config.h.generic"; exit 1; }
	@cp $(SRCDIR)/config.h.generic $@ && echo "  STAGE $@"

$(SRCDIR)/pcre2_chartables.c:
	@test -f $(SRCDIR)/pcre2_chartables.c.dist || { echo "ERROR: no $(SRCDIR)/pcre2_chartables.c.dist"; exit 1; }
	@cp $(SRCDIR)/pcre2_chartables.c.dist $@ && echo "  STAGE $@"

# One compile rule per code-unit width. Generated files are order-only so a
# newer template never forces a pointless rebuild.
define WIDTH_RULE
$(OBJDIR)/%.$(1).o: $(SRCDIR)/%.c | $(GEN) $(OBJDIR)
	@echo "  CC[$(1)] $$<"
	@$$(COMPILE) -DPCRE2_CODE_UNIT_WIDTH=$(1) -c $$< -o $$@
endef
$(foreach w,8 16 32,$(eval $(call WIDTH_RULE,$(w))))

$(LIB8): $(OBJ_8) | $(BUILD)
	@rm -f $@ && $(AR) rcs $@ $^ && echo "  AR    $@"
$(LIB16): $(OBJ_16) | $(BUILD)
	@rm -f $@ && $(AR) rcs $@ $^ && echo "  AR    $@"
$(LIB32): $(OBJ_32) | $(BUILD)
	@rm -f $@ && $(AR) rcs $@ $^ && echo "  AR    $@"

$(BUILD)/libpcre2-posix.a: $(OBJDIR)/pcre2posix.8.o | $(BUILD)
	@rm -f $@ && $(AR) rcs $@ $^ && echo "  AR    $@"

$(TEST): $(SRCDIR)/pcre2test.c $(LIBS) | $(GEN) $(BUILD)
	@echo "  LINK  $@"
	@$(COMPILE) $< -o $@ $(LIBS) $(LDFLAGS) $(LDLIBS)
	@echo "  ---- $@ ----" && $@ -C 2>&1 | sed 's/^/  /'

# Run the checkout's own RunTest against build/pcre2test.
RTDIR := $(BUILD)/rt
runtest: $(TEST)
	@rm -rf $(RTDIR) && mkdir -p $(RTDIR)
	@cp $(TEST) $(RTDIR)/pcre2test
	@for b in 8 16 32; do \
	   mkdir -p $(RTDIR)/testoutput$$b $(RTDIR)/testoutput$$b-jit $(RTDIR)/testoutput$$b-dfa; \
	 done
	@cd $(RTDIR) && srcdir="$(CURDIR)" pcre2test="$(CURDIR)/$(RTDIR)/pcre2test" \
	   sh "$(CURDIR)/RunTest"

srclist:
	@echo "compiled ($(words $(NAMES))):"; printf '  %s\n' $(NAMES)
	@echo "excluded:";                     printf '  %s\n' $(EXCLUDE)

clean:
	@rm -rf $(BUILD)

# Remove staged files only when byte-identical to their template, so a
# candidate-authored config.h / pcre2.h survives.
distclean: clean
	@for f in pcre2.h config.h; do \
	   if [ -f $(SRCDIR)/$$f ] && [ -f $(SRCDIR)/$$f.generic ] && \
	      cmp -s $(SRCDIR)/$$f $(SRCDIR)/$$f.generic; then rm -f $(SRCDIR)/$$f; fi; \
	 done
	@if [ -f $(SRCDIR)/pcre2_chartables.c ] && \
	    cmp -s $(SRCDIR)/pcre2_chartables.c $(SRCDIR)/pcre2_chartables.c.dist; then \
	   rm -f $(SRCDIR)/pcre2_chartables.c; fi
