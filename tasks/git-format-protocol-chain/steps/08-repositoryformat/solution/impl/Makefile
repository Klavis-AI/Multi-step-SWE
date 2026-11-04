# Build the PHP git implementation: generate the launcher and the dashed-form
# links under libexec/git-core, exactly as git's own layout expects.
PREFIX := $(CURDIR)/out
SRC := $(CURDIR)/src

BUILTINS := $(shell cat $(SRC)/builtins.txt 2>/dev/null)

all: $(PREFIX)/bin/git links

$(PREFIX)/bin/git: $(SRC)/boot.php Makefile
	@mkdir -p $(PREFIX)/bin $(PREFIX)/libexec/git-core
	@printf '%s\n' \
	  '#!/bin/sh' \
	  '# resolve the repository root from this script, so the tree can move' \
	  'self=$$0' \
	  'while [ -L "$$self" ]; do self=$$(readlink "$$self"); done' \
	  'root=$$(CDPATH= cd -- "$$(dirname -- "$$self")/../.." && pwd)' \
	  '# OPcache file cache: a PHP git otherwise re-parses every source on each' \
	  '# invocation, which dominates test suites that run thousands of gits.' \
	  'cache=$${GIT_PHP_OPCACHE:-$${TMPDIR:-/tmp}/.gitphp-opc-$$(id -u 2>/dev/null || echo 0)}' \
	  'mkdir -p "$$cache" 2>/dev/null || cache=' \
	  'if [ -n "$$cache" ]; then' \
	  '  exec php -d error_reporting=E_ALL -d display_errors=stderr -d opcache.enable_cli=1 -d opcache.file_cache="$$cache" -d opcache.file_cache_only=1 "$$root/src/boot.php" "$$@"' \
	  'fi' \
	  'exec php -d error_reporting=E_ALL -d display_errors=stderr "$$root/src/boot.php" "$$@"' > $@
	@chmod +x $@

links: $(PREFIX)/bin/git
	@mkdir -p $(PREFIX)/libexec/git-core
	@for c in $(BUILTINS); do \
	   printf '%s\n' '#!/bin/sh' \
	     'root=$$(CDPATH= cd -- "$$(dirname -- "$$0")/../.." && pwd)' \
	     "exec \"\$$root/bin/git\" $$c \"\$$@\"" > $(PREFIX)/libexec/git-core/git-$$c; \
	   chmod +x $(PREFIX)/libexec/git-core/git-$$c; \
	 done
	@printf '%s\n' '#!/bin/sh' \
	  'root=$$(CDPATH= cd -- "$$(dirname -- "$$0")/../.." && pwd)' \
	  'exec "$$root/bin/git" "$$@"' > $(PREFIX)/libexec/git-core/git
	@chmod +x $(PREFIX)/libexec/git-core/git

check:
	@php -l $(SRC)/boot.php >/dev/null && echo "syntax ok"

clean:
	rm -rf $(PREFIX)

.PHONY: all links check clean
