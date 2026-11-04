#!/usr/bin/env bash
# Unsetting a multi-value option clears every value stored under the name.
source /tests/caselib.sh

run_driver se_option_count temp v1 v2 || fail "se_option_count exited $?"
need_kv survived 1 "driver ran to completion"
need_kv count 2 "two values stored before the unset"
need_kv unset_rc 0 "SE_UnsetOption succeeded"
need_kv count_cleared 0 "the count is zero after the unset"
