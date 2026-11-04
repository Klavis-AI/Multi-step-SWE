#!/usr/bin/env bash
# Build the per-function measurement harness against /app/srsran.
#
#   /app/tools/build_harness.sh [<build-dir>]      (default /app/build)
#
# The harness is compiled from /app/tools/bench_phy.cpp against the tree's own
# headers and static libraries, so it calls whatever implementation the tree
# installs -- which is what the grader times.
set -eu
BLD="${1:-/app/build}"
SRC=/app/srsran
g++ -std=gnu++17 -O2 -fno-rtti -march=x86-64-v3 -mtune=generic -DASSERTS_ENABLED \
    -I "$SRC/include" -I "$SRC/external/fmt/include" -I "$SRC/external" \
    -o /app/tools/bench_phy /app/tools/bench_phy.cpp \
    -Wl,--start-group \
    "$BLD/lib/phy/upper/channel_coding/ldpc/libsrsran_ldpc.a" \
    "$BLD/lib/phy/upper/channel_coding/libsrsran_channel_coding.a" \
    "$BLD/lib/phy/upper/channel_coding/libsrsran_crc_calculator.a" \
    "$BLD/lib/phy/upper/channel_coding/polar/libsrsran_polar.a" \
    "$BLD/lib/phy/upper/channel_coding/short/libsrsran_short_block.a" \
    "$BLD/lib/phy/upper/channel_modulation/libsrsran_channel_modulation.a" \
    "$BLD/lib/phy/generic_functions/precoding/libsrsran_channel_precoder.a" \
    "$BLD/lib/ofh/compression/libsrsran_ofh_compression.a" \
    "$BLD/lib/phy/upper/liblog_likelihood_ratio.a" \
    "$BLD/lib/srsvec/libsrsvec.a" \
    "$BLD/lib/srslog/libsrslog.a" \
    "$BLD/lib/support/libsrsran_support.a" \
    "$BLD/lib/ran/libsrsran_ran.a" \
    "$BLD/external/fmt/libfmt.a" \
    /usr/lib/x86_64-linux-gnu/libyaml-cpp.so.0.7.0 \
    -Wl,--end-group \
    -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lm
echo "built /app/tools/bench_phy"
