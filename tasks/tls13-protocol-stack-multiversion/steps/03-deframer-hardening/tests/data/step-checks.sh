# step-checks.sh -- step 03
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here, two observables.
#
# 1. A zero-length fragment of a record whose type is not application_data is
#    refused instead of being read through. OpaqueMessage::read in
#    rustls/src/msgs/message.rs gains
#
#        if typ != ContentType::ApplicationData && len == 0 {
#            return Err(MessageError::IllegalLength);
#        }
#
#    in this release; it is absent from the 0.20.0 tree the candidate inherits.
#    Both citations are in the seeded corpus: RFC 8446 section 5.1 and RFC 5246
#    section 6.2.1 each forbid sending a zero-length fragment of a handshake,
#    alert or change_cipher_spec record. Measured with
#    bin/send-clienthello.py --mode empty-first and --mode empty-mid against the
#    example server, three runs per level:
#
#      level 01 (0.19.0)  result=handshake detail=server_hello  x3
#      level 02 (0.20.0)  result=handshake detail=server_hello  x3
#      level 03 (0.20.7)  result=none detail="server closed without sending a
#                         record"                               x3
#      levels 04-13       result=alert detail="fatal decode_error"
#
#    The group therefore uses the `refuse` expectation, not `reject`: what
#    changed here is that the empty fragment stops being accepted, and whether
#    the refusal carries a courtesy alert or is a bare close is the
#    implementation's choice -- 03 closes, 04 and later alert, and `refuse`
#    holds at every level from 03 up, so the group can be inherited unchanged.
#
# 2. A peer named by an IP address literal can be connected to at all.
#    ServerName in rustls/src/client/client_conn.rs grows an IpAddress form and
#    TryFrom<&str> falls back to parsing the string as an address, so a client
#    pointed at 127.0.0.1 now builds a ClientHello and sends it. Measured with
#    bin/observe-clienthello.py, three runs per level:
#
#      levels 01-02  samples: 0  failures: 1 -- the client panics on
#                    InvalidDnsNameError before any socket is written to
#      levels 03-13  samples: 1  failures: 0, with an empty server_name:
#
#    Only the connectability is graded. Whether a server_name extension is sent
#    for an IP peer does not change anywhere on the ladder (it is never sent),
#    so there is nothing there to grade, and the instruction says so.
#
# Inherited here and new at step 02: a handshake header declaring a length above
# the 64 kB bound is refused rather than buffered. Level 01 leaves such a hello
# unanswered; 02 alerts; 03 alerts. `refuse` covers alert and close alike, so
# this re-check holds for every later step too and steps 04-13 should carry it.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-one
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: record-empty-fragment-refused
# GROUP: clienthello-ip-peer-hello-sent

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-one 1
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group record-empty-fragment-refused refuse empty-first empty-mid
    ch_ip_peer_group clienthello-ip-peer-hello-sent hello
}
