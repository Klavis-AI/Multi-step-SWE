# PostgreSQL-compatible server.  `make` builds bin/postgres and bin/initdb.
PGVC_MAJOR ?= 96
CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-function
CPPFLAGS += -DPGVC_MAJOR=$(PGVC_MAJOR) -Isrc -MMD -MP
LDLIBS  += -lpthread -lm

SRVOBJS = src/util.o src/version.o src/value.o src/numeric.o src/dtime.o src/jsonval.o \
          src/jsonpath.o \
          src/digest.o src/unicode.o src/deparse.o src/lex.o src/parse.o src/catalog.o src/guc.o src/exec.o \
          src/funcs.o src/support.o src/txn.o src/proto.o src/copy.o src/repl.o \
          src/plpgsql.o src/xmltable.o src/tsearch.o src/tzdata.o \
          src/store.o src/main.o
INITOBJS = src/initdb.o src/version.o

all: bin/postgres bin/initdb

bin/postgres: $(SRVOBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(SRVOBJS) $(LDLIBS)

bin/initdb: $(INITOBJS)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(INITOBJS) $(LDLIBS)

# Rebuild everything when the target major version changes.
.pgvc_major: FORCE
	@echo '$(PGVC_MAJOR)' | cmp -s - $@ || echo '$(PGVC_MAJOR)' > $@
FORCE:

%.o: %.c .pgvc_major
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o src/*.d bin/postgres bin/initdb .pgvc_major

-include $(SRVOBJS:.o=.d) $(INITOBJS:.o=.d)

.PHONY: all clean FORCE
