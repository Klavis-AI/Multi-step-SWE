# Redis-compatible server, implemented in Java.
#
#   make            build everything and (re)generate the launchers
#   make clean      remove build output
#
# The only third-party dependency is LuaJ, provided by the image at
# /opt/redis-deps. Nothing is downloaded at build time.

JAVAC   ?= javac
DEPS    := /opt/redis-deps/luaj-jse-3.0.1.jar
CLASSES := build/classes
SOURCES := $(shell find src/main/java -name '*.java' | sort)
RES     := $(wildcard src/main/resources/*)

.PHONY: all clean

all: src/redis-server src/redis-sentinel

$(CLASSES)/.stamp: $(SOURCES) $(RES)
	@mkdir -p $(CLASSES)
	$(JAVAC) -nowarn -encoding ISO-8859-1 -cp "$(DEPS)" -d $(CLASSES) $(SOURCES)
	@if [ -d src/main/resources ]; then cp -a src/main/resources/. $(CLASSES)/; fi
	@touch $@

src/redis-server: $(CLASSES)/.stamp
	@mkdir -p src
	@printf '%s\n' \
	  '#!/bin/sh' \
	  '# Redis-compatible server launcher. Resolves its own real path so it also' \
	  '# works when invoked through a symlink from another working directory, and' \
	  '# execs the JVM so the pid the caller sees is the server process.' \
	  'SELF=$$(readlink -f "$$0")' \
	  'ROOT=$$(dirname "$$(dirname "$$SELF")")' \
	  'exec java -XX:+UseSerialGC -XX:+ExitOnOutOfMemoryError -XX:TieredStopAtLevel=1 -Xms16m -Xmx768m -Xss8m \' \
	  '  -Dfile.encoding=ISO-8859-1 -Djava.awt.headless=true \' \
	  '  -cp "$$ROOT/build/classes:/opt/redis-deps/luaj-jse-3.0.1.jar" redis.Main "$$@"' \
	  > src/redis-server
	@chmod +x src/redis-server

src/redis-sentinel: $(CLASSES)/.stamp
	@mkdir -p src
	@printf '%s\n' \
	  '#!/bin/sh' \
	  '# redis-sentinel: the same server started in Sentinel mode, the way redis' \
	  '# does it when argv[0] is redis-sentinel.' \
	  'SELF=$$(readlink -f "$$0")' \
	  'ROOT=$$(dirname "$$(dirname "$$SELF")")' \
	  'exec java -XX:+UseSerialGC -XX:+ExitOnOutOfMemoryError -XX:TieredStopAtLevel=1 -Xms16m -Xmx768m -Xss8m \' \
	  '  -Dfile.encoding=ISO-8859-1 -Djava.awt.headless=true \' \
	  '  -cp "$$ROOT/build/classes:/opt/redis-deps/luaj-jse-3.0.1.jar" redis.Main "$$@" --sentinel' \
	  > src/redis-sentinel
	@chmod +x src/redis-sentinel

clean:
	rm -rf build src/redis-server src/redis-sentinel
