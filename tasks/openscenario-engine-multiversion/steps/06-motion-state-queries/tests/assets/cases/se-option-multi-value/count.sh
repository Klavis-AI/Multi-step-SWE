#!/usr/bin/env bash
# A multi-value option counts the values stored under one name. Setting the
# same name three times makes the count three, and the value read back is the
# most recent one.
source /tests/caselib.sh

run_driver se_option_count opt alpha beta gamma || fail "se_option_count exited $?"
need_kv survived 1 "driver ran to completion"

need_kv count_unset 0 "an option that was never set counts zero"
need_kv count_never 0 "a name that was never set counts zero"

need_kv n_set 3 "three values were stored"
need_kv count 3 "the count matches the number of stored values"
need_kv read_value gamma "the value read back is the most recent one"

# A single-valued option is the same mechanism with one entry, which a stub
# returning a constant cannot satisfy alongside the three above.
run_driver se_option_count single only_one || fail "se_option_count exited $?"
need_kv n_set 1 "one value stored"
need_kv count 1 "count is one"
need_kv read_value only_one "value read back"
