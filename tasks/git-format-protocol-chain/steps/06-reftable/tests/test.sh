#!/usr/bin/env bash
# Verifier entry point (identical in every step; the step's data lives in
# tests/fixtures).  run.py always writes reward.json and exits 0.
set -u
mkdir -p /logs/verifier
PYTHONDONTWRITEBYTECODE=1 python3 -B /tests/verify/run.py 2>&1 | tee /logs/verifier/verifier.log
exit 0
