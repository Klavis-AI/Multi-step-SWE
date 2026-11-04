#!/usr/bin/env sh
# Unpack this milestone's published FHIR packages into the FHIR package cache.
set -eu
CACHE=${FHIR_PACKAGE_CACHE:-/root/.fhir/packages}
here=$(cd "$(dirname "$0")" && pwd)
for t in "$here"/*.tgz; do
  b=$(basename "$t" .tgz)
  # the version begins at the first "-" followed by a digit, so a version
  # that itself contains a hyphen (6.0.0-ballot3) still parses correctly
  name=$(printf %s "$b" | sed -E 's/-[0-9].*$//')
  ver=${b#"$name"-}
  d="$CACHE/$name#$ver"
  [ -d "$d/package" ] && continue
  mkdir -p "$d" && tar xzf "$t" -C "$d"
  echo "installed $name#$ver"
done
