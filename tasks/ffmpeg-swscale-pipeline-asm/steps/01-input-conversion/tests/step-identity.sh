#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '01' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
07f000166299acb36c4f4209c6d5aa6536a75a02a7d47a9c225eb1a17cd454ad  FUNCS
88c2291cec5eac3c8633d83a807a9958978c135437d355b1d8464e2e59f7fa4a  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
646d13a56c10d61f5b53d8f2490d881269c9dd9376a984e05477575b5bf230c9  STAGE
SUMS
# context-uniqueness padding .
