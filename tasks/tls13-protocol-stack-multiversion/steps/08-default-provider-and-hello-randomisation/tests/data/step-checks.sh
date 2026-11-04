# step-checks.sh -- step 08
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here, three capabilities, each scored by its own group:
#
#   * The ClientHello's extension ordering is randomised per connection. Six
#     connections are observed; a fixed order yields exactly one distinct
#     ordering and a shuffled one effectively never does.
#
#   * The unbuffered connection API ships as a worked example server, so the
#     probe drives that binary and speaks to it with openssl s_client. What is
#     scored is what comes back over the socket: a TLS 1.3 handshake and an
#     HTTP response, not the shape of the API behind it.
#
#   * An acceptor that rejects a ClientHello reports the refusal on the wire.
#     Accepted::into_connection hands back the alert for a rejected hello, so
#     the first record the peer sees is a fatal alert rather than a silent
#     close. The probe reads the record's content type and level, never the
#     description, because which description is correct is the library's
#     choice.
#
# All three obligations are monotone -- randomised ordering stays randomised,
# the example keeps working, the alert keeps being sent -- so every later step
# inherits them.
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
# GROUP: inherited-psk-modes-required-with-psk
# GROUP: inherited-tls12-client-empty-session-id
# GROUP: inherited-clienthello-sct-request-withdrawn
# GROUP: inherited-certreq-authorities-omitted-tls13
# GROUP: inherited-certreq-ca-hints-tls12
# GROUP: inherited-certreq-ca-hints-tls13
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: clienthello-extension-order-randomised
# GROUP: unbuffered-server-interop
# GROUP: acceptor-refusal-alert

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-four 4
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    ip_peer_cert_group inherited-ip-peer-cert-verified
    server_psk_modes_group inherited-psk-modes-required-with-psk abort
    ch_session_id_group inherited-tls12-client-empty-session-id tls12 empty
    ch_extension_group inherited-clienthello-sct-request-withdrawn absent 18 signed_certificate_timestamp
    shim_certreq_group inherited-certreq-authorities-omitted-tls13 tls13 - absent
    shim_ca_hint_group inherited-certreq-ca-hints-tls12 tls12 "$CA_HINT_DER" empty
    shim_ca_hint_group inherited-certreq-ca-hints-tls13 tls13 "$CA_HINT_DER" absent
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    ch_extension_order_group clienthello-extension-order-randomised randomised
    unbuffered_server_group unbuffered-server-interop
    acceptor_alert_group acceptor-refusal-alert alert
}
