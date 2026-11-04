#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '04' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
d637b04fd89982fecaa8662edabb7120cc29874bd94e373698bcce217de29e6c  FUNCS
864d25dac1e7d116b11545e190af2466464a088ec77004a276ce3fe70223ed8a  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
e0f3fa6b37a499afd8689d1bf180c51f3867111094446bb5c1788fb1f5c71cab  STAGE
SUMS
# context-uniqueness padding ....
