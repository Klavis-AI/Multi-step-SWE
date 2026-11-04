#!/usr/bin/env bash
# Install the published FHIR NPM packages into the standard FHIR package cache
# (~/.fhir/packages), the layout every FHIR tool uses.
set -euo pipefail
CACHE=${FHIR_PACKAGE_CACHE:-/root/.fhir/packages}
mkdir -p "$CACHE"
while IFS='|' read -r name version url sha; do
  case "$name" in ''|\#*) continue ;; esac
  dest="$CACHE/${name}#${version}"
  [ -d "$dest/package" ] && continue
  tmp=$(mktemp)
  for attempt in 1 2 3 4 5; do
    if curl -sSfL --retry 3 --max-time 600 -o "$tmp" "$url"; then break; fi
    sleep $((attempt * 10))
  done
  echo "${sha}  ${tmp}" | sha256sum -c - >/dev/null
  mkdir -p "$dest"
  tar xzf "$tmp" -C "$dest"
  rm -f "$tmp"
  echo "installed ${name}#${version}"
done < "$(dirname "$0")/fhir-packages.txt"
