#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '02' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
4c517748476b6e1936c2333dcca2c611fe1a0592a4dfc3856b3db3fff93e3045  FUNCS
1351344024a245989443cd7f6385666e4873d3a966008d9c5f773e74d983a36a  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
21b553d2388730c9e01b6bbd4177151015a15051898f4868a0ae0f04aad1df1a  STAGE
SUMS
# context-uniqueness padding ..
