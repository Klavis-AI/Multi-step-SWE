# step-checks.sh -- step 06
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# The capability *withdrawn* at this step -- SCT delivery by TLS extension --
# is graded on both sides of the connection. The BoGo withdrawal groups cover
# the server side: the shim must decline the cases that ask it to serve an
# SCT list, rather than merely not pass them. The group below covers the
# client side, which BoGo cannot see: 0.22 stops *asking* for SCTs, so
# extension 18 (signed_certificate_timestamp, RFC 6962 3.3.1) disappears from
# the ClientHello the example client writes. At 0.21.6 it is there in every
# hello, so this group separates the two trees on the wire rather than by API
# shape.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# Two of the inherited groups below arrived at step 05 and are re-checked here
# for the first time: the PSK-without-modes refusal and the empty
# legacy_session_id of a TLS-1.2-only client. This step is the largest refactor
# on the ladder -- every certificate call site moves to pki-types and every
# verifier is rebuilt -- which is exactly the change that silently drops a
# handshake-time check, and the step's own instruction promises that every
# out-of-band check an earlier step introduced runs again here.
#
# GROUP: clienthello-sct-request-withdrawn
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-four
# GROUP: inherited-psk-modes-required-with-psk
# GROUP: inherited-tls12-client-empty-session-id
# GROUP: inherited-clienthello-ip-peer-hello-sent
# GROUP: inherited-ip-peer-cert-verified
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused

step_extra_checks() {
    ch_extension_group clienthello-sct-request-withdrawn absent 18 signed_certificate_timestamp
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-four 4
    server_psk_modes_group inherited-psk-modes-required-with-psk abort
    ch_session_id_group inherited-tls12-client-empty-session-id tls12 empty
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    ip_peer_cert_group inherited-ip-peer-cert-verified
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
}
