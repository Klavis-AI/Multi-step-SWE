#!/usr/bin/env bash
# Two ways a distribution file is refused, both with a negative status and with
# the process staying alive to report the state it is actually in rather than
# aborting: a document that parses as XML but describes an impossible
# distribution, and a document that is not well-formed XML at all.
#
# Only the SIGN of the status is asserted. The instruction promises "a negative
# value"; which negative value is not a requirement a candidate was given.
source /tests/caselib.sh

# 1. Parses, but the range runs backwards: lowerLimit 30 above upperLimit 10.
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_badrange.xosc" 0 || fail "se_param_dist exited $? rather than returning a status"
need_neg load "SE_SetParameterDistribution on a range whose lower limit exceeds its upper"
need_kv n_perm 0 "no permutations are enumerated for a refused distribution"
need_kv survived 1 "the engine survives a refused distribution"

# 2. Not a well-formed document: the element nesting is never closed, so the
# XML parse itself fails. This is the "cannot be read" rejection of req 1, and
# it must be a return value rather than a crash.
run_driver se_param_dist "$FIXTURES/param_cars.xosc" "$FIXTURES/dist_malformed.xosc" 0 || fail "se_param_dist exited $? on an unparseable file rather than returning a status"
need_neg load "SE_SetParameterDistribution on a file that is not well-formed XML"
need_kv n_perm 0 "no permutations are enumerated for an unparseable file"
need_kv survived 1 "the engine survives an unparseable distribution file"
