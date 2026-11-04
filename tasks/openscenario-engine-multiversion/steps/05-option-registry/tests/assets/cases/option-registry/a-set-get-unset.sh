#!/usr/bin/env bash
# New. The runtime option registry's basic lifecycle: a presence-flag option
# set by name alone, a string-valued option set and read back, and an unset
# that clears the flag again.
source /tests/caselib.sh

cp "$FIXTURES/elev_cruise.xosc" "$WORK/elev_cruise.xosc"
cp "$FIXTURES/curves_elevation.xodr" "$WORK/curves_elevation.xodr"

run_driver d_options elev_cruise.xosc
need_status 0 $? "d_options"

need_kv pre_set 0 "option reads unset before anything is stored"
need_kv pre_val "(null)" "unset option has no value"
need_kv flag_set 1 "presence-flag option reads as set after SE_SetOption"
need_kv num_set 1 "string-valued option reads as set after SE_SetOptionValue"
need_kv num_val 42.5 "string-valued option round-trips its value"
need_kv word_val hello "a non-numeric value round-trips too"
need_kv roundtrip 1 "value read back matches the value stored"
need_kv flag_after_unset 0 "SE_UnsetOption clears the flag"

# R2's one documented REJECTION: storing a value under a name already registered
# as a pure presence flag with no argument slot. R2 also says which call creates
# such a name -- SetOption / SetOptionPersistent register a previously-unseen
# name without an argument slot -- so the probe reaches the failure using only
# entry points R2 defines. The SIGN is asserted and the magnitude is not, which
# is exactly what R2 promises; the success side is `rm_set_rc 0` below.
need_neg pureflag_reject_rc "SE_SetOptionValue is rejected on a name registered as a pure presence flag"
need_neg rm_pureflag_reject_rc "RM_SetOptionValue is rejected the same way on the mirrored entry point"

# R05.21: the RM_ family carries "the same signatures and semantics" as the SE_
# one, and this group previously reached only RM_SetOption and RM_GetOptionSet.
# The value and unset halves are asserted here, and every one of them runs with
# NO scenario loaded -- this driver never calls RM_Init, and its first use of the
# store is before the SE_ side has loaded anything either, which is the "and no
# scenario loaded" half of the row that the shared-store case could not isolate.
need_kv rm_pre_set 0 "RM_GetOptionSet reads unset before anything is stored"
need_kv rm_set 1 "RM_SetOptionValue marks the option set"
need_kv rm_set_rc 0 "RM_SetOptionValue reports success on a name it registers itself"
need_kv rm_shared_se_sees 1 "SE_GetOptionSet sees an option RM_SetOptionValue stored"
need_kv rm_shared_se_val alpha "SE_GetOptionValue reads back the value RM_SetOptionValue stored"
need_kv rm_keep_before 1 "RM_SetOptionPersistent marks the option set"
need_kv rm_pure_unset 0 "RM_UnsetOption clears a pure option"
need_kv survived 1 "driver completed"
exit 0
