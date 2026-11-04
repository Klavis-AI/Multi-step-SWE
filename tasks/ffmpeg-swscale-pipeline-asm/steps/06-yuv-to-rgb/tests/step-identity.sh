#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '06' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
ccf99ca480ef8303415af49b0a534e8a3bf7edac914eff9bfab37038b398d3fa  FUNCS
e530b30120a51695e32f2b2b3b38ba81fee7d059c9dbd4e725ae87f421fa0ae6  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
992c970836accfb06223df4225824e5e7d639c4c71d604f5a9c356c12d9cbb99  STAGE
SUMS
# context-uniqueness padding ......
