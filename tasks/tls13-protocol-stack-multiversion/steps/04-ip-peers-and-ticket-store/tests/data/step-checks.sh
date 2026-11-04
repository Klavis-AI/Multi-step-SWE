# step-checks.sh -- step 04
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# New here, and both are graded:
#
#   * the certificate of a peer named by an IP address is verified against the
#     iPAddress entry in its subjectAltName. 0.20.7 refuses every such peer
#     outright -- `UnsupportedNameType`, before any certificate is looked at --
#     so the step-03 tree fails this group and the step-04 tree passes it. The
#     negative leg (the same client, a certificate that names only DNS hosts)
#     is what keeps it from being passable by not checking names at all.
#
#   * the server's default NewSessionTicket count changes from one to four.
#     This is a library default, not a configuration the example sets, so it is
#     read straight off the wire.
#
# The count is graded per step and never inherited: it changes again at step 11
# (four to two), so an inherited "sends four" would fail steps 11-13 on their
# own Oracle.
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: inherited-diff-openssl-server
# GROUP: inherited-diff-openssl-client
# GROUP: inherited-sni-illegal-name-refused
# GROUP: inherited-record-framing-split-accepted
# GROUP: inherited-clienthello-ip-peer-hello-sent
# GROUP: inherited-handshake-oversize-message-refused
# GROUP: inherited-record-empty-fragment-refused
# GROUP: ip-peer-cert-verified
# GROUP: tickets-default-four

step_extra_checks() {
    diff_server_group inherited-diff-openssl-server
    diff_client_group inherited-diff-openssl-client reject-unverifiable
    server_sni_group inherited-sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group inherited-record-framing-split-accepted accept fragmented byte-split
    ch_ip_peer_group inherited-clienthello-ip-peer-hello-sent hello
    server_framing_group inherited-handshake-oversize-message-refused refuse huge-hs
    server_framing_group inherited-record-empty-fragment-refused refuse empty-first empty-mid
    ip_peer_cert_group ip-peer-cert-verified
    ticket_count_group tickets-default-four 4
}
