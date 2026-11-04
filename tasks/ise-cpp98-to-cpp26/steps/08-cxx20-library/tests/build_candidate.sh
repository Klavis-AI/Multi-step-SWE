#!/usr/bin/env bash
# Configure and build the tree under /app, from clean, as the unprivileged
# user. Written by authoring/render_package.py; edit authoring/verifier/.
#
# Driven the way CMake's own documentation drives it -- `cmake -S <src> -B
# <dir>` then `cmake --build <dir>` -- and with no knowledge of how the project
# arranges its targets. Where the artefacts land is read back afterwards by
# isefix.stage, by name.
set -u

SRC=${SRC:-/app}
BUILD=${BUILD:-/app/build}

# One third per call: `clean`, `configure`, `compile`. They are separate
# because root does something between each pair, and the whole of that defence
# is that no candidate process is running while it happens -- it digests the
# tree after the sweep and opens the trace sink before the configure
# (snapshot_tree, open_trace_sink), and stamps the build graph between the
# configure and the build (stamp_build_graph). All three are in helpers.sh.
# `all` is the three in one call, for a reader driving this script by hand;
# the verifier never uses it.
#
# `sanitize` is a fourth, optional phase: the same sources, configured and
# built a second time into a directory of the verifier's own with the
# compiler's allocation checking switched on, for the step that asks about
# ownership. It is separate from the three above because it must not be what
# gets scored -- the graded build is the one the task describes, with the
# options the task states and nothing else -- and because root takes nothing
# between its halves, so its configure and its build are one call.
PHASE=${1:-all}
case "$PHASE" in
    clean | configure | compile | sanitize | all) ;;
    *) echo "usage: build_candidate.sh" \
            "[clean|configure|compile|sanitize|all]" >&2
       exit 2 ;;
esac

if [ "$PHASE" = clean ] || [ "$PHASE" = all ]; then

echo "=== toolchain"
cmake --version | head -1
clang++ --version | head -1
echo "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE:-<unset>}"

# Anything the agent left behind that looks like build output is removed before
# the build, so that a stale or hand-placed binary cannot be what gets scored,
# and so that a CMake cache written in a different container cannot decide this
# configure. Source files are never touched: only caches, and only files whose
# names are the ones the build is expected to produce.
echo "=== clearing prior build output"
python3 -B - "$SRC" <<'PYTHON'
import os, shutil, sys

src = sys.argv[1]
ARTEFACTS = {
    "libise.a", "libise.so", "4in1_server", "chargen_client", "chargen_server",
    "daytime_server", "discard_server", "echo_server", "hello_world",
    "http_server", "max_connection", "server_inspector", "server_module_msgs",
    "thread_pool", "timer", "udp_client", "udp_server", "isedrv",
}
CACHES = {"CMakeCache.txt", "CMakeFiles", "cmake_install.cmake",
          "compile_commands.json", ".ninja_deps", ".ninja_log"}
removed = 0
for root, dirs, files in os.walk(src, topdown=True):
    for name in list(dirs):
        if name in CACHES:
            shutil.rmtree(os.path.join(root, name), ignore_errors=True)
            dirs.remove(name)
            removed += 1
    for name in files:
        if name in ARTEFACTS or name in CACHES or name.endswith((".o", ".obj")):
            try:
                os.remove(os.path.join(root, name))
                removed += 1
            except OSError:
                pass
print("removed %d stale build products" % removed)
PYTHON

fi

if [ "$PHASE" = configure ] || [ "$PHASE" = all ]; then

# Literally the two commands the task states, and nothing else. An argument
# added here is a requirement the task never made: -DCMAKE_EXPORT_COMPILE_COMMANDS
# would mean a project that never enables it is still graded as though it had,
# and -j would mean the verifier builds the project in a way the candidate was
# never told about. Both now live in the image's environment instead, where
# they apply equally to the candidate's own builds.
#
# The three trace arguments are the exception, and they are not a requirement
# either: --trace-expand changes what CMake PRINTS, not what it does. They are
# here rather than on a second configure of the grader's own because a second
# configure is a second thing to tell apart from the first, and a project that
# can tell them apart can run the retired command in the one that is graded
# and not in the one that is read. That was measured: a tree wrapping one call
# in `if(NOT DEFINED ENV{ISEFIX_TRACE_PATH})` scored 14 of 14 while doing the
# thing gate five exists to catch. There is now one configure. It is the one
# that produces the build, and it is the one the gates read.
#
# ISEFIX_TRACE_PATH and the preload go on THIS command and not on the phase:
# set for the whole script, the guard would be loaded by this shell first and
# by the python above, each of which would unset the pair from its own
# environment before cmake ever saw them. Also, they must not reach the
# compiler: the guard belongs in the one process that opens the trace.
#
# Whether to ask for a trace is read off fd 3 and not out of the environment.
# test.sh points fd 3 at the sink on the configure and at /dev/null on the
# clean and compile phases, so the test below is true exactly once per scored
# build -- and an environment that said the same thing would still be saying
# it when ninja runs `cmake --regenerate-during-build` inside the compile
# phase, where no guard unsets it. A listfile that can tell the regenerate
# from the graded configure is a listfile that can do its retired work in the
# one that is not read. The descriptor tells it nothing: it is the same
# /dev/null in both untraced phases and unreadable as a name in the traced
# one. The two constants are plain shell variables, never exported.
TRACE_PATH=/dev/fd/3
TRACE_GUARD=/opt/ise/traceguard.so
echo "=== cmake -S $SRC -B $BUILD -DCMAKE_BUILD_TYPE=Debug"
if [ -f "$TRACE_PATH" ]; then
    echo "=== tracing into $TRACE_PATH"
    LD_PRELOAD="$TRACE_GUARD${LD_PRELOAD:+ }${LD_PRELOAD:-}" \
    ISEFIX_TRACE_PATH="$TRACE_PATH" \
        cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
            --trace-expand --trace-format=json-v1 \
            "--trace-redirect=$TRACE_PATH" || exit 10
else
    cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug || exit 10
fi
echo "=== configured"

fi

if [ "$PHASE" = compile ] || [ "$PHASE" = all ]; then

echo "=== cmake --build $BUILD"
cmake --build "$BUILD" || exit 11

echo "=== done"

fi

if [ "$PHASE" = sanitize ]; then

# The second build. Same sources, same toolchain, same two commands; the only
# difference is two cache entries carrying the compiler's allocation checking,
# and they are the per-configuration forms rather than the plain ones. That is
# not a style choice. CMAKE_CXX_FLAGS is where the toolchain file puts
# --gcc-install-dir, without which nothing in this image compiles at all, so
# setting THAT on the command line replaces the flag the build needs with the
# flags this phase wants and the configure fails on the first test program.
# The _DEBUG forms are empty until something fills them, and the two are
# concatenated.
#
# Into a directory outside /app, because /app has already been digested by the
# time this runs and everything this writes would otherwise read as the tree
# rewriting itself between the compiler and the scorer.
#
# -fsanitize-recover=address, with halt_on_error=0 at run time, so that a
# program that trips the checker finishes its session and the next thirty-one
# sessions still run. Without it the first error ends the process and every
# session after it is scored against a tree nothing further was learned about.
#
# -O0, which is the graded build's own optimisation level, and NOT the -O1 the
# sanitizer's documentation suggests. Measured, not chosen: at -O1 this build
# fails from step 9 onward with
#
#   ise_socket.h:301:14: error: TU local entity '__bswap_32' is exposed
#                               [-Werror,-WTU-local-entity-exposure]
#
# because glibc defines ntohl and htons as macros over `static __inline`
# __bswap_32/__bswap_16 when __OPTIMIZE__ is defined, and a module interface
# unit may not expose a TU-local entity. The same tree builds clean under the
# same instrumentation at -O0, where the macros are not in play, and the
# graded build never sees the diagnostic at all because CMake's Debug
# configuration carries no -O flag.
#
# So at -O1 this phase does not report an unclean tree; it reports a tree that
# could not be instrumented, and the one gate that reads it fails for a reason
# no agent following the instruction could have seen in their own build. An
# instrumented build whose optimisation level differs from the graded build's
# is measuring a second dialect rather than the ownership it was written to
# measure, so it matches.
SANBUILD=${SANBUILD:-/tmp/isesan/build}
SANFLAGS="-fsanitize=address -fsanitize-recover=address"
SANFLAGS="$SANFLAGS -fno-omit-frame-pointer -g"

echo "=== cmake -S $SRC -B $SANBUILD (instrumented)"
rm -rf "$SANBUILD" || exit 12
mkdir -p "$SANBUILD" || exit 12
cmake -S "$SRC" -B "$SANBUILD" -DCMAKE_BUILD_TYPE=Debug \
    "-DCMAKE_CXX_FLAGS_DEBUG=-O0 $SANFLAGS" \
    "-DCMAKE_EXE_LINKER_FLAGS_DEBUG=$SANFLAGS" || exit 12

echo "=== cmake --build $SANBUILD"
cmake --build "$SANBUILD" || exit 13

echo "=== instrumented build done"

fi
