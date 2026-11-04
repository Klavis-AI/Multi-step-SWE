#!/usr/bin/env bash
# An option carrying several values becomes readable one value at a time, with
# the count reported separately. Reading past the end, or reading a name that was
# never set, must yield nothing rather than a stale or fabricated string.
source /tests/caselib.sh

run_driver se_option_values path 51 dirA dirB dirC
need_status 0 $? "se_option_values"

# Before anything is stored the option has no values and index 0 reads as null.
need_kv count_before 0 "value count before any value is set"
need_kv idx0_before_null 1 "index 0 reads null before any value is set"

need_kv all_set_ok 1 "all three values stored"
need_kv count 3 "value count after storing three values"

# The store hands values back most-recently-set first. The order is asserted
# explicitly because an implementation that returns them in insertion order is a
# different, and observably wrong, contract.
need_kv idx_0 dirC "value at index 0"
need_kv idx_1 dirB "value at index 1"
need_kv idx_2 dirA "value at index 2"
need_kv all_distinct 1 "the three indices returned three different values"

need_kv past_end_null 1 "index past the end reads null"
need_kv unknown_null 1 "an option name that was never set reads null"
need_kv survived 1 "driver completed"
exit 0
