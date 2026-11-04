# step-checks.sh -- step 13
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here: a record carrying a ClientHello followed by another handshake
# message is refused with a fatal alert. This is GHSA-2mjx-qc3c-rqvc: steps
# 04-09 refused such a record, the deframer rewrite at step 10 began accepting
# it, and the alignment check extended by #3265 refuses it again. Steps 10-12
# were graded correct without it; at this step accepting it is a defect.
#
# Two groups probe this, one per deframer path. The packed-record group
# drives a single record carrying both messages (the take_handshake_message
# path); the reassembled-record group drives a ClientHello split across two
# records whose closing fragment appends the trailing message (the coalesce
# path). Only the second definition of the alignment check -- the one that
# covers messages already buffered but not yet processed -- refuses the
# latter, so the reassembled-record group is the one that turns red on the
# previous step's tree.
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
# GROUP: inherited-kx-mlkem768-preferred-with-key-share
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
# GROUP: inherited-kx-mlkem768-server-selects-on-retry
# GROUP: inherited-kx-mlkem768-share-well-formed
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: handshake-alignment-packed-record-refused
# GROUP: handshake-alignment-reassembled-record-refused

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74
step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group inherited-tickets-default-two 2
    server_sni_group inherited-sni-ip-literal-accepted accept '127.0.0.1' '::1'
    flight_shape_group inherited-flight-coalesced-single-record coalesced
    kx_offer_group inherited-kx-mlkem768-preferred-with-key-share X25519MLKEM768 preferred-with-share
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
    early_data_group inherited-early-data-cap-advertised-and-read 2048
    kx_server_retry_group inherited-kx-mlkem768-server-selects-on-retry X25519MLKEM768 selects
    kx_hybrid_share_group inherited-kx-mlkem768-share-well-formed well-formed
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    server_framing_group handshake-alignment-packed-record-refused reject packed
    server_reassembly_alignment_group handshake-alignment-reassembled-record-refused
}
