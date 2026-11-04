#!/usr/bin/env bash
# Inherited. The option store's set/get/unset surface predates this step and must
# survive it unchanged.
#
# Deliberately NOT asserted here: the return code for setting an option name that
# was never registered. It is 0 at v2.46.4 and -1 at v2.51.1, so it is a real
# behavioural change inside this step's range and not part of the inherited
# contract. Asserting it in either direction would produce a guaranteed false
# result at one end. Do not "fix" this by adding it back.
source /tests/caselib.sh

run_driver se_option_store osc myfile.xosc headless
need_status 0 $? "se_option_store"

need_kv value_before "(empty)" "value before anything is stored"
need_kv value_after myfile.xosc "value after storing it"
need_kv flag_after_value_set 1 "the option reads as set once it carries a value"
need_kv flag_after 1 "the option reads as set after the flag form is used"
need_kv value_cleared "(empty)" "value after unsetting"
need_kv survived 1 "driver completed"
exit 0
