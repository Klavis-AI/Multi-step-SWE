# step-checks.sh -- step 02
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here: a handshake message whose header declares a length above the 64 kB
# bound is refused rather than buffered. This is the observable half of the
# milestone's "buffers are bounded at 64 kB" headline -- MAX_HANDSHAKE_SIZE in
# rustls/src/msgs/hsjoiner.rs, which does not exist in the tree the candidate
# inherits. Measured with bin/send-clienthello.py --mode huge-hs against the
# example server, three runs per level:
#
#   level 01 (0.19.0)  result=none detail="no reply within Ns" x3 -- the joiner
#                      keeps waiting for a ~16 MB message and the peer hangs
#   level 02 (0.20.0)  result=alert detail="fatal decode_error" x3 -- the
#                      declared length is checked against the bound and the
#                      message is rejected on its header
#
# The group uses the `refuse` expectation, not `reject`: what changed at this
# step is that the hello stops being left unanswered, and the choice between
# alerting and closing is the implementation's. Steps 02-09 alert and steps
# 10-13 close; both refuse, so the change at 09->10 is not graded as a
# capability. The 01->02 difference is refusing against hanging, and `refuse`
# is written so that the same group holds at every later step, alert or close
# alike.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-one
# GROUP: handshake-oversize-message-refused

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-one 1
    server_framing_group handshake-oversize-message-refused refuse huge-hs
}
