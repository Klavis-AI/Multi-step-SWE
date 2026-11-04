# step-checks.sh -- step 10
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# The step's headline behaviour is the early-data cap, and one half of it *is*
# reachable from outside BoGo: a server willing to read 0-RTT data has to say
# so in every NewSessionTicket, and the number it says is the number it was
# configured with. `early-data-cap-advertised-and-read` reads that number back
# out of an ordinary client's session dump and then resumes the ticket with
# early data on it. It is deliberately two-legged -- off by default, exactly
# the configured limit when switched on -- because either leg alone is passable
# without the capability; the reasoning is on early_data_group in
# bin/checks-lib.sh. The previous step's example server has no
# `--max-early-data` flag at all, so its tree fails this group outright.
#
# The deframer rewrite stays with BoGo's robustness and early-data cases, which
# are where record buffering is observable at all.
#
# Not scored here: what a server does when a client sends *more* early data
# than the advertised cap. That is graded, by BoGo, whose shim can be told to
# overrun; it cannot be graded here because `openssl s_client` enforces the
# advertised limit itself and will not write past it, so the probe would be
# measuring OpenSSL. TEST_INTERFACE.md section 6 covers this.
#
# Not scored here either: a record carrying a ClientHello plus a trailing
# handshake message. This step accepts one and steps 04-09 refused it; that is
# GHSA-2mjx-qc3c-rqvc, not a capability, and it is graded at step 13.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: early-data-cap-advertised-and-read
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-four
# GROUP: inherited-sni-ip-literal-accepted
# GROUP: inherited-clienthello-ip-peer-hello-sent
# GROUP: inherited-ip-peer-cert-verified
# GROUP: inherited-psk-modes-required-with-psk
# GROUP: inherited-tls12-client-empty-session-id
# GROUP: inherited-clienthello-sct-request-withdrawn
# GROUP: inherited-certreq-authorities-omitted-tls13
# GROUP: inherited-certreq-ca-hints-tls12
# GROUP: inherited-certreq-ca-hints-tls13
# GROUP: inherited-acceptor-refusal-alert
# GROUP: inherited-unbuffered-server-interop
# GROUP: inherited-sni-ip-literal-disregarded
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-four 4
    server_sni_group inherited-sni-ip-literal-accepted accept '127.0.0.1' '::1'
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    ip_peer_cert_group inherited-ip-peer-cert-verified
    server_psk_modes_group inherited-psk-modes-required-with-psk abort
    ch_session_id_group inherited-tls12-client-empty-session-id tls12 empty
    ch_extension_group inherited-clienthello-sct-request-withdrawn absent 18 signed_certificate_timestamp
    shim_certreq_group inherited-certreq-authorities-omitted-tls13 tls13 - absent
    shim_ca_hint_group inherited-certreq-ca-hints-tls12 tls12 "$CA_HINT_DER" empty
    shim_ca_hint_group inherited-certreq-ca-hints-tls13 tls13 "$CA_HINT_DER" absent
    acceptor_alert_group inherited-acceptor-refusal-alert alert
    unbuffered_server_group inherited-unbuffered-server-interop
    server_sni_ack_group inherited-sni-ip-literal-disregarded localhost '127.0.0.1' '::1'
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    # 2048 rather than the 14336 the BoGo shim configures, and deliberately so:
    # the value is read back off the wire and compared, so a tree that hardcodes
    # the one number this package mentions elsewhere does not pass by accident.
    early_data_group early-data-cap-advertised-and-read 2048
}
