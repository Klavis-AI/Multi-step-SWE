#!/usr/bin/env bash
# The storyboard-element state-change callback must actually be invoked for
# every state change the fixture's storyboard makes, not merely accepted and
# ignored: a no-op registration would leave every count at zero.
#
# The three counts below are the arithmetic of instruction.md §6 applied to a
# fixture whose act holds one manoeuvre group, holding one manoeuvre, holding
# one event, holding one action. §6 states the rules that produce them, so they
# are derivable rather than an inventory of one implementation's behaviour:
#
#   * "fires exactly once each time an element's state is genuinely different"
#     -- so this is a count of STATE CHANGES, not two per element;
#   * "an element that never occupies RUNNING is reported only as COMPLETE"
#     -- the action completes in the step that activates it, and so does the
#     event holding it, giving each of them one call and no running call;
#   * "an element still running when the scenario ends is not reported
#     complete" -- the act is running when the stop trigger fires, so it is
#     reported running and never complete;
#   * "a manoeuvre ... reports running then complete" -- §6's one required
#     change, worth two calls.
#
# Running: act, group, manoeuvre = 3. Complete: group, manoeuvre, event,
# action = 4. Total 7. Each of the first three rules is inherited behaviour,
# measured identical at v2.23.1 and v2.27.0; only the manoeuvre's two calls are
# what this step adds, which is why the group fails at the starting tag.
source /tests/caselib.sh

run_driver sb_callback "$FIXTURES/param_cars.xosc" || fail "sb_callback exited $?"
need_kv init 0 "the fixture scenario initialises"
need_kv n_calls 7 "one callback per storyboard state change the scenario makes"
need_kv n_running 3 "the act, its manoeuvre group and its manoeuvre each start running once"
need_kv n_complete 4 "the group, the manoeuvre, the event and its action all reach complete"
need_kv survived 1 "the engine keeps running after the callback is registered"

# instruction.md section 6: a registration made before SE_Init does not survive
# it, so a host application registers after the scenario is initialised. The
# same fixture is run again with the registration moved ahead of the init: the
# run must still play to its stop trigger and deliver nothing at all.
run_driver sb_callback "$FIXTURES/param_cars.xosc" preinit || fail "sb_callback exited $?"
need_kv preinit 1 "the driver registered the callback before SE_Init"
need_kv init 0 "the fixture scenario initialises with a registration pending"
need_kv n_calls 0 "a pre-init registration is cleared by SE_Init and delivers no calls"
need_kv survived 1 "the engine keeps running after a pre-init registration"
