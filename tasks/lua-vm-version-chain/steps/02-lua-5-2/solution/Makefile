# Build the Lua interpreter.
#
# MYCFLAGS carries compile-time macro definitions, the way src/Makefile does in
# the reference distribution: everything in it that looks like -DNAME[=VALUE]
# becomes a compile-time setting the interpreter can query.

# The macro definitions this release's own src/Makefile compiles with. MYCFLAGS
# is appended after them, exactly as CFLAGS does, so a later -U or -D wins.
DEFS= -DLUA_COMPAT_ALL
MYCFLAGS=
JAVAC= javac
JFLAGS= -nowarn -g:none

SRC := $(shell find src -name '*.java')

all: lua

.PHONY: all clean conf

conf:
	@mkdir -p gen/lua
	@{ \
	  echo 'package lua;'; \
	  echo ''; \
	  echo '/** Compile-time configuration, generated from MYCFLAGS. */'; \
	  echo 'final class Conf {'; \
	  echo '    private static final java.util.List<String> D ='; \
	  echo '        java.util.Arrays.asList(new String[] {'; \
	  for d in $(DEFS) $(MYCFLAGS); do \
	    case "$$d" in \
	      -D*) echo "            \"$${d#-D}\","; ;; \
	      -U*) echo "            \"!$${d#-U}\","; ;; \
	    esac; \
	  done; \
	  echo '            "" });'; \
	  echo ''; \
	  echo '    /** The macro'"'"'s value after the whole command line, or null if undefined. */'; \
	  echo '    static String value(String macro) {'; \
	  echo '        String v = null;'; \
	  echo '        for (String d : D) {'; \
	  echo '            if (d.startsWith("!")) { if (d.substring(1).equals(macro)) v = null; continue; }'; \
	  echo '            int i = d.indexOf(61);'; \
	  echo '            if (i > 0) { if (d.substring(0, i).equals(macro)) v = d.substring(i + 1); }'; \
	  echo '            else if (d.equals(macro)) v = "1";'; \
	  echo '        }'; \
	  echo '        return v;'; \
	  echo '    }'; \
	  echo ''; \
	  echo '    /** True when the macro is defined and not defined to 0. */'; \
	  echo '    static boolean on(String macro) {'; \
	  echo '        String v = value(macro);'; \
	  echo '        return v != null && !v.equals("0");'; \
	  echo '    }'; \
	  echo ''; \
	  echo '    /** True unless the macro is explicitly defined to 0. */'; \
	  echo '    static boolean onByDefault(String macro) {'; \
	  echo '        String v = value(macro);'; \
	  echo '        return v == null || !v.equals("0");'; \
	  echo '    }'; \
	  echo '}'; \
	} > gen/lua/Conf.java.new
	@cmp -s gen/lua/Conf.java.new gen/lua/Conf.java || mv gen/lua/Conf.java.new gen/lua/Conf.java
	@rm -f gen/lua/Conf.java.new

build/.stamp: conf $(SRC)
	@mkdir -p build
	$(JAVAC) $(JFLAGS) -d build $(SRC) gen/lua/Conf.java
	@touch build/.stamp

lua: build/.stamp lua.in
	@cp lua.in lua
	@chmod +x lua

clean:
	rm -rf build gen lua
