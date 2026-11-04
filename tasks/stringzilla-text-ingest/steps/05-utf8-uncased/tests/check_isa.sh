#!/usr/bin/env bash
# Capability envelope, scoped to this step's own family.  Sourced by /tests/test.sh.
#
# check_isa <tree> <family> [<base>]
#
# The scan itself lives in check_isa.py, because stripping comments out of C
# before looking for intrinsics is not something to re-derive in shell.
#
# With a base tree the check only judges the files the candidate added or
# changed in this step's scope; files byte-identical to the base are the base's
# own code and the scan reports itself vacuous for them.  Without a base tree
# every scoped file is scanned.
check_isa() {
    python3 /tests/vendor/check_isa.py "$@" || return 1
    return 0
}
