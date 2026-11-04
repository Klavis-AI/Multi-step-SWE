# Makefile -- builds the interpreter into sapi/cli/php.
#
# The release this build emulates is set once, here.  PHP_VERSION_STRING is
# what PHP_VERSION reports, PHP_VERSION_ID is the numeric form the engine
# compares against, and PHP_TARGET_ID is the same number under the name the
# parser uses; all three come from the two variables below, so re-targeting
# the build is a one-line edit.

PHP_TARGET_VERSION = 7.0.33
PHP_TARGET_ID      = 70033

CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
override CPPFLAGS += -I Zend -I main -I . \
	-DPHP_VERSION_STRING='"$(PHP_TARGET_VERSION)"' \
	-DPHP_VERSION_ID=$(PHP_TARGET_ID) \
	-DPHP_TARGET_ID=$(PHP_TARGET_ID)
LDLIBS = -lm

SRCS = $(wildcard Zend/*.c) $(wildcard main/*.c) $(wildcard ext/*/*.c) \
       $(wildcard sapi/cli/*.c)
OBJS = $(SRCS:.c=.o)

all: sapi/cli/php

sapi/cli/php: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) sapi/cli/php

.PHONY: all clean
