# step-checks.sh -- step 11
#
# Non-BoGo groups for this step. Every group here is recorded once, as a pass
# or a fail, by exactly one call; the tier and weight each carries come from
# data/groups.tsv, which is what the scorer reads; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# Five new observables, all read off the wire:
#
#   - X25519MLKEM768 is offered, but it is neither the first preference nor
#     given a key share. "Supported at low negotiation priority" is a statement
#     about position, so position is what is graded.
#   - the server can actually *use* X25519MLKEM768: offered that group alone,
#     with no key share, it answers with a HelloRetryRequest naming it rather
#     than with handshake_failure.
#   - the client can actually *produce* an X25519MLKEM768 share: asked by
#     HelloRetryRequest to use the group it offered without a share, it sends
#     the 1216 bytes that group's definition fixes.
#   - the server's default NewSessionTicket count changes from four to two.
#   - the encrypted handshake flight arrives as a single record rather than one
#     record per message.
#
# The middle three are the "listed" / "implemented" distinction. A name in
# supported_groups costs nothing to add, so the offer check alone cannot tell a
# release that added the group from one that added the word. The two retry
# probes can, and they need no cryptography of their own: both answers arrive
# in cleartext in the peer's second message.
#
# Four of the five are graded per step and not inherited. At step 12 the hybrid
# group becomes the preference and does carry a key share, which makes
# kx-mlkem768-offered-not-preferred false there, and makes
# kx-mlkem768-client-share-on-retry unanswerable there for the reason RFC 8446
# sec 4.2.8 gives: a client that already sent the share cannot legally be asked
# to retry with it. The ticket count changes again at step 12. Flight coalescing
# and the server-side retry are monotone; only flight coalescing is written as
# inherited here, because the server probe is new at this step and inherits
# nothing.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: tickets-default-two
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
# GROUP: inherited-early-data-cap-advertised-and-read
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: flight-coalesced-single-record
# GROUP: kx-mlkem768-offered-not-preferred
# GROUP: kx-mlkem768-server-selects-on-retry
# GROUP: kx-mlkem768-client-share-on-retry

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ticket_count_group tickets-default-two 2
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
    early_data_group inherited-early-data-cap-advertised-and-read 2048
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    flight_shape_group flight-coalesced-single-record coalesced
    kx_offer_group kx-mlkem768-offered-not-preferred X25519MLKEM768 offered-last
    kx_server_retry_group kx-mlkem768-server-selects-on-retry X25519MLKEM768 selects
    kx_retry_share_group kx-mlkem768-client-share-on-retry X25519MLKEM768 1216
}
