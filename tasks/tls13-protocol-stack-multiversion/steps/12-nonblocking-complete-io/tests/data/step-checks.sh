# step-checks.sh -- step 12
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here: X25519MLKEM768 becomes the client's first preference and spends a
# key share on it. Step 11's rule -- offered but not preferred -- is deliberately
# absent rather than inherited, because this step contradicts it.
#
# Two groups, not one, because "prefers the group" and "can actually perform
# it" fail separately and an implementation can reach the first without the
# second. kx_offer_group reads names -- which group is first and which one a
# key_share entry is labelled with -- and a name costs nothing to emit. The
# share itself is what a peer has to do arithmetic on, and
# draft-ietf-tls-ecdhe-mlkem sec 4.1 fixes its size and the order of its two
# halves; kx_hybrid_share_group reads that. Both come out of the same
# six-connection sweep, so the second point costs no extra handshake.
#
# Inherited from step 11, which introduced it: that the server can actually
# perform X25519MLKEM768 rather than only list it, answering a hello that
# offers that group alone with an empty client_shares with a HelloRetryRequest
# naming it. Step 11's other new retry group -- the *client's* share size
# measured after an HRR -- is deliberately not inherited and cannot be: from
# this step the client already spends a share on the group, so RFC 8446 sec
# 4.2.8 makes a retry request for it illegal and a correct client rejects it.
# The share is read straight out of the first hello here instead, by
# kx_hybrid_share_group above.
#
# Re-read here, as one of the four inherited defaults the instruction names:
# the order of the client's ClientHello extensions must still vary between
# connections. It is a GATE group like the other inherited defaults -- it
# vetoes rather than scoring -- and it reads the same six-hello sweep the
# extension-18 group already pays for.
#
# Not scored here: the alert description for a syntactically invalid hostname
# changes from decode_error to illegal_parameter at this step. It is a real
# change, but the illegal-name group is graded description-agnostically at all
# thirteen steps, and pinning the description here would add trivia rather than
# coverage.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-tickets-default-two
# GROUP: inherited-sni-ip-literal-accepted
# GROUP: inherited-flight-coalesced-single-record
# GROUP: kx-mlkem768-preferred-with-key-share
# GROUP: inherited-kx-mlkem768-server-selects-on-retry
# GROUP: inherited-clienthello-extension-order-randomised
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
# GROUP: inherited-early-data-cap-advertised-and-read
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: kx-mlkem768-share-well-formed

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-two 2
    server_sni_group inherited-sni-ip-literal-accepted accept '127.0.0.1' '::1'
    flight_shape_group inherited-flight-coalesced-single-record coalesced
    kx_offer_group kx-mlkem768-preferred-with-key-share X25519MLKEM768 preferred-with-share
    kx_server_retry_group inherited-kx-mlkem768-server-selects-on-retry X25519MLKEM768 selects
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    ip_peer_cert_group inherited-ip-peer-cert-verified
    server_psk_modes_group inherited-psk-modes-required-with-psk abort
    ch_session_id_group inherited-tls12-client-empty-session-id tls12 empty
    ch_extension_group inherited-clienthello-sct-request-withdrawn absent 18 signed_certificate_timestamp
    ch_extension_order_group inherited-clienthello-extension-order-randomised randomised
    shim_certreq_group inherited-certreq-authorities-omitted-tls13 tls13 - absent
    shim_ca_hint_group inherited-certreq-ca-hints-tls12 tls12 "$CA_HINT_DER" empty
    shim_ca_hint_group inherited-certreq-ca-hints-tls13 tls13 "$CA_HINT_DER" absent
    acceptor_alert_group inherited-acceptor-refusal-alert alert
    unbuffered_server_group inherited-unbuffered-server-interop
    server_sni_ack_group inherited-sni-ip-literal-disregarded localhost '127.0.0.1' '::1'
    early_data_group inherited-early-data-cap-advertised-and-read 2048
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    kx_hybrid_share_group kx-mlkem768-share-well-formed well-formed
}
