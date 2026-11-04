#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '05' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
fd0415a460400fb1e3b5a234581452f9ac1f5e152153fe2e01544fe497b52a14  FUNCS
23bcc8bd130559553c1b9b25d1a57d0887748698352782c7805b9df75819cd1e  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
2f261e4df3e85a0137ef013e86ff3969d6b2fa725ae9903afa92b2f33f4103e0  STAGE
SUMS
# context-uniqueness padding .....
