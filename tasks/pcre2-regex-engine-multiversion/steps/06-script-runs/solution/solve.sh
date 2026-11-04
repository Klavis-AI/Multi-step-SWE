#!/usr/bin/env bash
set -euo pipefail

# Oracle for this milestone: install upstream PCRE2 10.33's src/ tree over the
# cumulative /app workspace. Nothing is cloned, downloaded or fetched.
#
#   upstream release   PCRE2 10.33, 2019-04-16
#   commit             08db4900bcedc3438d56a4c8ea15016cff9fdbf5
#   licence            BSD-3-Clause WITH PCRE2-exception (see pcre2-doc/)

solution_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
app_dir=${HARBOR_APP_DIR:-/app}

( cd "$solution_dir" && sha256sum -c SHA256SUMS )

cd "$app_dir"

# Everything the task owns lives outside src/: the build Makefile, pcre2-doc/,
# testdata/, unicode-data/ and the two contract documents. Replacing src/
# wholesale is therefore safe and is what makes the ladder's seven opcode
# renumberings a non-event for the Oracle.
rm -rf -- build src
tar xzf "$solution_dir/src.tar.gz" -C "$app_dir"

test -f "$app_dir/src/pcre2test.c"
test -f "$app_dir/src/pcre2_compile.c"
test -f "$app_dir/src/pcre2_match.c"
test -f "$app_dir/src/pcre2.h.generic"
test -f "$app_dir/src/config.h.generic"
test -f "$app_dir/src/pcre2_chartables.c.dist"
test -f "$app_dir/Makefile"
test -f "$app_dir/TEST_INTERFACE.md"
test -f "$app_dir/WORKSPACE_HANDOFF.md"
test -d "$app_dir/pcre2-doc"
test -d "$app_dir/testdata"

grep -q 'PCRE2_MAJOR' "$app_dir/src/pcre2.h.generic"

printf 'installed PCRE2 %s src/ (%s, commit %s)\n' '10.33' '2019-04-16' '08db4900bcedc3438d56a4c8ea15016cff9fdbf5'
