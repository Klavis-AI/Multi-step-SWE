#!/bin/bash
# Configure and build the tree in place.
#
#   ./build.sh [builddir]        default: /app/ffmpeg/build
#
# The flag set is fixed on purpose: it holds HAVE_INLINE_ASM at 0 and
# HAVE_X86ASM at 1, so the only thing that varies between milestones is which
# optimized kernels are present, and it switches off everything the task does
# not touch. Everything the milestone is graded on is built by `make checkasm`.
#
# Extra arguments are passed through to configure, so a build directory can be
# kept per experiment; do not change the flags themselves, since a tree that
# only builds under a different configuration cannot be graded.
set -eu
# Compile through ccache when the image provides it: the same object files are
# built at every milestone, so the cache turns a full rebuild into a relink.
if [ -d /usr/lib/ccache ]; then
    PATH=/usr/lib/ccache:$PATH
    export PATH
fi
SRC=${SRC:-/app/ffmpeg}
BLD=${1:-$SRC/build}
JOBS=$(nproc); [ "$JOBS" -gt 8 ] && JOBS=8
mkdir -p "$BLD"
cd "$BLD"
[ -f config.mak ] || "$SRC/configure" \
    --disable-everything --enable-swscale --enable-checkasm \
    --disable-doc --disable-programs --disable-network --disable-autodetect \
    --disable-inline-asm --disable-vulkan --disable-cuda --disable-cuda-llvm \
    --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-vaapi \
    --disable-vdpau --disable-libdrm
make -j"$JOBS"
make -j"$JOBS" checkasm
echo "built: $BLD/tests/checkasm/checkasm"
