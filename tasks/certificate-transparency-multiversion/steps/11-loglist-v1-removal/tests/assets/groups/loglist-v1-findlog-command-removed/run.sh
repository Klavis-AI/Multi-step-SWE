#!/usr/bin/env bash
# loglist-v1-findlog-command-removed
#
# loglist/findlog was a `package main` command in the product at v1.1.3; v1.1.4
# deletes it along with the rest of the v1 log-list tree, and ships no
# replacement command. The withdrawal of a *command* is observed the same way
# the withdrawal of a package is: the build target stops resolving, with a
# positive control (a command the milestone keeps) proving the toolchain and
# the module graph are sound.
set -u

n=0; f=0
pass() { printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; n=$((n + 1)); }
fail() { printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; n=$((n + 1)); f=$((f + 1)); }
finish() { printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"; exit 0; }

export GOFLAGS=-mod=mod GOWORK=off GOPROXY=off GOSUMDB=off GOMAXPROCS=4

mod="$CT_SCRATCH/mod"
mkdir -p "$mod"
cp -R "$CT_ASSETS/verifier/." "$mod/" 2>/dev/null || true

gone='does not contain package'
findlog='github.com/google/certificate-transparency-go/loglist/findlog'
keeper='github.com/google/certificate-transparency-go/ctutil/sctcheck'

# --- Control: a product command this milestone still builds ---------------
if ( cd "$mod" && timeout 900 go build -o "$CT_SCRATCH/sctcheck.bin" "$keeper" ) > "$CT_SCRATCH/keeper.out" 2>&1; then
	pass control-surviving-command-builds
else
	fail control-surviving-command-builds "$keeper did not build: $(tr '\n' ' ' < "$CT_SCRATCH/keeper.out" | cut -c1-400)"
	finish
fi

# The control has to have produced a real executable, or "it built" is empty.
if [ -x "$CT_SCRATCH/sctcheck.bin" ] && [ -s "$CT_SCRATCH/sctcheck.bin" ]; then
	pass control-surviving-command-is-executable
else
	fail control-surviving-command-is-executable "no executable was produced for $keeper"
	finish
fi

# --- Probe: the v1 log-list command is no longer a build target -----------
if ( cd "$mod" && timeout 900 go build -o "$CT_SCRATCH/findlog.bin" "$findlog" ) > "$CT_SCRATCH/findlog.out" 2>&1; then
	fail findlog-command-not-a-build-target "$findlog still builds"
elif grep -q "$gone $findlog\$" "$CT_SCRATCH/findlog.out"; then
	pass findlog-command-not-a-build-target
else
	fail findlog-command-not-a-build-target "build failed without naming $findlog: $(tr '\n' ' ' < "$CT_SCRATCH/findlog.out" | cut -c1-400)"
fi

# --- Probe: nor does anything else live under the v1 log-list tree --------
# findlog was one package inside loglist/; the release removes the directory,
# not just the two paths named above. The toolchain's own wildcard settles it:
# the v1 subtree matches no package at all, while the successor subtree still
# matches at least one, so a wildcard that silently matched nothing everywhere
# could not make this pass. (The parent package's own disappearance is
# loglist-v1-package-removed's subject, not this group's.)
prod='github.com/google/certificate-transparency-go'
( cd "$mod" && timeout 600 go list "$prod/loglist/..." ) > "$CT_SCRATCH/v1tree.txt" 2> "$CT_SCRATCH/v1tree.err" || true
( cd "$mod" && timeout 600 go list "$prod/loglist3/..." ) > "$CT_SCRATCH/v3tree.txt" 2> "$CT_SCRATCH/v3tree.err" || true
survivors=$(grep -c "^$prod/loglist/" "$CT_SCRATCH/v1tree.txt" 2>/dev/null || true)
[ -n "$survivors" ] || survivors=0
if [ "$survivors" -eq 0 ] && grep -q "^$prod/loglist3" "$CT_SCRATCH/v3tree.txt"; then
	pass no-package-survives-under-the-v1-log-list-tree
elif [ "$survivors" -ne 0 ]; then
	fail no-package-survives-under-the-v1-log-list-tree "still present: $(tr '\n' ' ' < "$CT_SCRATCH/v1tree.txt" | cut -c1-400)"
else
	fail no-package-survives-under-the-v1-log-list-tree "the successor subtree did not list either: $(tr '\n' ' ' < "$CT_SCRATCH/v3tree.err" | cut -c1-400)"
fi

finish
