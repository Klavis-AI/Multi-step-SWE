# Specification corpus

Thirteen documents, copied byte-for-byte from the RFC Editor and the IETF
datatracker. **They are verbatim and unmodified.** If one of them contradicts
anything else in this workspace, the specification is what counts.

## Which document answers which question

| Question | Document |
| --- | --- |
| TLS 1.3 handshake, key schedule, record layer, alerts, extensions | `rfc8446.txt` |
| TLS 1.2 handshake and ciphersuite behaviour | `rfc5246.txt` |
| `server_name`, `status_request`, `max_fragment_length` | `rfc6066.txt` |
| `extended_master_secret`, the session hash, and when a peer must abort | `rfc7627.txt` |
| `application_layer_protocol_negotiation`: offer, selection, mismatch alert | `rfc7301.txt` |
| Stateless resumption and the ticket format | `rfc5077.txt` |
| Signed Certificate Timestamps and how they are delivered | `rfc6962.txt` |
| Certificate path validation, name constraints, CRLs | `rfc5280.txt` |
| Which code point means what; the IANA registries | `rfc8447.txt` |
| Raw public keys instead of certificates | `rfc7250.txt` |
| Record-layer and epoch vocabulary | `rfc9147.txt` |
| `record_size_limit` | `rfc8449.txt` |
| X25519MLKEM768 hybrid key exchange, share layout, code point | `draft-ietf-tls-ecdhe-mlkem.txt` |

## Reading order for the handshake

RFC 8446 §4 is the handshake; §7 is the key schedule; §5 is the record layer;
§6 is alerts; §9.2 lists the extensions a compliant implementation must
handle. Appendix C.4 is the client-side rule about how many times a ticket may
be used. Appendix D covers backward compatibility with TLS 1.2, which
`rfc5246.txt` specifies in full.

## Provenance and licence

See `LICENSE-NOTICE.md` in this directory.
