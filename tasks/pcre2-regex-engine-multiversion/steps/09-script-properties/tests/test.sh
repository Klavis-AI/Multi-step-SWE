#!/usr/bin/env bash
# Step 09 -- Script properties and the silent redefinition (upstream pcre2-10.42)
#
# Scoring is reward = gate x score; both halves are declared in
# /tests/manifest.tsv and executed by /tests/runner.sh. Nothing in this file
# decides a number, so the contract can be read off the manifest alone.
set -u
export LC_ALL=C
source /tests/helpers.sh
# Before hardening, and before anything else: this runs no candidate code, and
# a mis-bound image should bail rather than grade the wrong milestone.
require_step 09

# Lock the reward directory before anything else. Every later command that
# touches candidate code goes through run_unprivileged.
harden_reward_dir

source /tests/runner.sh

PC_BUILD_TIMEOUT=900
PC_CASE_TIMEOUT=20

if pc_build; then
    pc_run_manifest
    pc_finish 0
else
    # The tree does not build. The gate is 0 by definition and no group is
    # attempted: a partial score over an unbuildable tree would be a fiction.
    pc_finish 1
fi
