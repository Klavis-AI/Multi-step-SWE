# step-checks.sh -- step 09
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here: a server_name holding an IP literal is parsed and then ignored,
# rather than refused as a decoding error. RFC 6066 does not permit an address
# there, but refusing to parse the extension breaks clients that send one
# anyway; the correct reading is to accept the message and disregard the entry.
#
# That rule has two halves and they are graded separately, because they fail
# separately. "Parsed" is visible in the first record: an address no longer
# draws a fatal alert. IPv4 and IPv6 get a group each -- they reach the name
# parser by different routes and an implementation can arrive at one without
# the other. "Disregarded" is not visible in the first record at all: a server
# that dispatched on the address would answer with a ServerHello too. It is
# visible in what the ServerHello carries, which is what the third group
# reads; see server_sni_ack_group in checks-lib.sh for why that probe drops to
# TLS 1.2 to do it.
#
# The illegal-name group is graded alongside all three for a reason: it is what
# stops a candidate from earning these points by no longer validating
# server_name at all. `ab@cd.com` and a bare `.` must still draw a fatal alert.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-accept-boundary
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-four
# GROUP: inherited-clienthello-ip-peer-hello-sent
# GROUP: inherited-ip-peer-cert-verified
# GROUP: inherited-psk-modes-required-with-psk
# GROUP: inherited-tls12-client-empty-session-id
# GROUP: inherited-clienthello-extension-order-randomised
# GROUP: inherited-clienthello-sct-request-withdrawn
# GROUP: inherited-certreq-authorities-omitted-tls13
# GROUP: inherited-certreq-ca-hints-tls12
# GROUP: inherited-certreq-ca-hints-tls13
# GROUP: inherited-acceptor-refusal-alert
# GROUP: inherited-unbuffered-server-interop
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: sni-ipv4-literal-accepted
# GROUP: sni-ipv6-literal-accepted
# GROUP: sni-ip-literal-disregarded

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
    ch_extension_order_group inherited-clienthello-extension-order-randomised randomised
    server_sni_group inherited-sni-accept-boundary accept 'exa_mple.com' 'example.com.'
    shim_certreq_group inherited-certreq-authorities-omitted-tls13 tls13 - absent
    shim_ca_hint_group inherited-certreq-ca-hints-tls12 tls12 "$CA_HINT_DER" empty
    shim_ca_hint_group inherited-certreq-ca-hints-tls13 tls13 "$CA_HINT_DER" absent
    acceptor_alert_group inherited-acceptor-refusal-alert alert
    unbuffered_server_group inherited-unbuffered-server-interop
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    server_sni_group sni-ipv4-literal-accepted accept '127.0.0.1'
    server_sni_group sni-ipv6-literal-accepted accept '::1'
    server_sni_ack_group sni-ip-literal-disregarded localhost '127.0.0.1' '::1'
}
