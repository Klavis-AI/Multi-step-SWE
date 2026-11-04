#!/bin/bash
# Writes this image's step identity and the digests of its per-step data files.
# Run as a single RUN layer by tests/Dockerfile, whose command text carries both
# the step number and these digests -- so unlike a COPY it cannot be served from
# a sibling step's build-context cache entry.
set -eu
printf '%s\n' '03' > /etc/harbor-step-id
cat > /etc/harbor-step-datasums <<'SUMS'
e476c5f69d1ba4d73b079aafa1703337457f53a285509a6994754026a80f3497  FUNCS
7af56a60ad89cddab3e2457c3edbbaab0ed492b6001d22081ee1b1caa97dfe50  ISA
1bf72d42d6139d11b9d6e0777a5a100b25b534d3c8bab24211ef5d3117408e79  E2E_TOL
f83da9a689b2aaa04ffb94f2058de71af991bb15a6d3056ba4d8c18f4c77d0c6  STAGE
SUMS
# context-uniqueness padding ...
