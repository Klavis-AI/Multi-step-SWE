#!/usr/bin/env bash
# Build the control plane, start it against a fresh etcd, replay the
# conformance corpus, and print per-area results.
set -u
# Nothing may leave a non-text file in the workspace: .pyc files would.
export PYTHONDONTWRITEBYTECODE=1
cd /app || exit 1
make -C /app || { echo "build failed"; exit 1; }
export KCP_KUBECTL="/usr/local/bin/kubectl-1.30"
exec python3 /app/conformance/run.py "$@"
