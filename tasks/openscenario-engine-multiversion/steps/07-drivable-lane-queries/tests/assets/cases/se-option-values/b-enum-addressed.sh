#!/usr/bin/env bash
# The same store is addressable by the option's entry in the configuration
# enumeration, so a caller holding the enum value need not know the spelling.
# The two addressing forms must agree on the same option.
source /tests/caselib.sh

# 51 is the configuration-enumeration entry for the option named "path".
run_driver se_option_values path 51 dirA dirB dirC
need_status 0 $? "se_option_values"
need_kv all_set_ok 1 "all three values stored"

# Reading by enum must land on the same option the name-addressed reader sees,
# and must agree with that reader's first index. The agreement flag is what
# makes this more than a second copy of the indexed test: it is computed inside
# the driver by comparing the two returned strings.
need_kv by_enum_last dirC "value read by enum"
need_kv by_enum_same_text_as_idx0 1 "enum-addressed read agrees with index 0"

# An enum value naming no option must yield nothing.
need_kv by_enum_unknown_null 1 "an enum value that names no option reads null"
need_kv survived 1 "driver completed"
exit 0
