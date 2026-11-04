# step-checks.sh -- step 01
#
# Non-BoGo groups for this step. Every group here is one point, scored by
# exactly one pass or fail; the BoGo groups are scored from the frozen case
# lists in data/bogo-groups/ and are not repeated here.
#
# This step establishes every non-BoGo obligation the rest of the ladder
# inherits: that both roles interoperate with OpenSSL, that a syntactically
# invalid hostname in server_name is refused, that a handshake message may be
# split across records, and that the server sends exactly one NewSessionTicket.
#
# Not scored here: the ordering of ClientHello extensions (it is stable at this
# step and randomised from step 08, and grading stability would make step 08 a
# withdrawal for no gain), and how a record carrying a trailing handshake
# message is treated (see step 13).
#
# Group names below are also the score-time tier: inherited-* is an inherited
# obligation, anything else is a new capability introduced at this step.
#
# GROUP: diff-openssl-server
# GROUP: diff-openssl-client
# GROUP: sni-illegal-name-refused
# GROUP: record-framing-split-accepted
# GROUP: tickets-default-one

step_extra_checks() {
    diff_server_group diff-openssl-server
    diff_client_group diff-openssl-client reject-unverifiable
    server_sni_group sni-illegal-name-refused reject 'ab@cd.com' '.'
    server_framing_group record-framing-split-accepted accept fragmented byte-split
    ticket_count_group tickets-default-one 1
}
