# step-checks.sh -- step 07
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# What is new at 0.22.2 and visible from outside is the issuer hint list a
# server puts in its CertificateRequest. Two things moved:
#
#   * the library. At TLS 1.3 the server used to push a
#     `certificate_authorities` extension unconditionally, empty when it had no
#     hints to give. It now leaves the extension out when the list is empty.
#     That is graded by `certreq-authorities-omitted-tls13`, which passes the
#     shim no new flag at all -- only `-require-any-client-certificate`, which
#     every release on this ladder accepts -- so the group turns on the library
#     and not on the shim's command line.
#
#   * the shim. It gained `-use-client-ca-list`, which is what makes a
#     *non-empty* list reachable from outside at all: the example server in
#     this workspace never asks for a client certificate, and the shim's
#     client-certificate verifier previously returned no hints whatever it was
#     told. The two `certreq-ca-hints-*` groups drive it, one per protocol
#     version, because the field is mandatory and plaintext at TLS 1.2 and an
#     optional encrypted extension at TLS 1.3, and those are different answers.
#
# The DER below is
# `SEQUENCE { SET { SEQUENCE { OID commonName, PrintableString "rustls-ca-hint" } } }`
# -- an issuer name that appears nowhere in the fixture PKI, so a server that
# echoed some name it had to hand could not produce it by accident.
#
# The rest re-check earlier steps.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: certreq-authorities-omitted-tls13
# GROUP: certreq-ca-hints-tls12
# GROUP: certreq-ca-hints-tls13
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
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused

CA_HINT_DER=3019311730150603550403130e727573746c732d63612d68696e74

step_extra_checks() {
    shim_certreq_group certreq-authorities-omitted-tls13 tls13 - absent
    shim_ca_hint_group certreq-ca-hints-tls12 tls12 "$CA_HINT_DER" empty
    shim_ca_hint_group certreq-ca-hints-tls13 tls13 "$CA_HINT_DER" absent
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
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
}
