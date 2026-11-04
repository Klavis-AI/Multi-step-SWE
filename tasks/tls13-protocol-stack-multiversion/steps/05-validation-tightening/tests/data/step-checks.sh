# step-checks.sh -- step 05
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here, both read off the wire:
#
#   * The server refuses a ClientHello that offers "pre_shared_key" without
#     "psk_key_exchange_modes". RFC 8446 4.2.9 makes the pairing mandatory on
#     the client and the abort mandatory on the server, and 0.21.6 is where
#     the check arrives; 0.21.5 answers the same hello with a ServerHello.
#     Only the level of the alert is graded, never its description.
#
#   * A client configured for TLS 1.2 only sends an empty legacy_session_id.
#     The 32 random bytes are middlebox-compatibility mode (RFC 8446 D.4) and
#     belong to TLS 1.3; a client that has been configured out of 1.3 has
#     nothing to be compatible with. The default client, which still offers
#     1.3, keeps its 32 bytes at this step and is not what this group reads.
#
# Both are graded per step rather than inherited, because both are the kind of
# default a later release can revisit and the ladder re-measures them where
# they matter.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-four
# GROUP: inherited-clienthello-ip-peer-hello-sent
# GROUP: inherited-ip-peer-cert-verified
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: psk-modes-required-with-psk
# GROUP: tls12-client-empty-session-id

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-four 4
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    ip_peer_cert_group inherited-ip-peer-cert-verified
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    server_psk_modes_group psk-modes-required-with-psk abort
    ch_session_id_group tls12-client-empty-session-id tls12 empty
}
