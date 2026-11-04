# PROFILE — step 07 of 13: key compliance and gossip (certificate-transparency-go v1.0.21)

## What this milestone adds over v1.0.19

**An escape hatch is removed, not defaulted off.** RFC 6962 allows a log's key
to be ECDSA on P-256 or RSA of at least 2048 bits, and the product refuses to
build a signature verifier over anything else. Until v1.0.19 that
refusal could be waived with `allow_verification_with_non_compliant_keys`, a
`flag` global declared in the *root* package and therefore present on every
binary's command line. At v1.0.21 the flag is gone from all of them. The
waiver survives only as an exported variable an importing program can set.

**A log gets an address.** `--handler_prefix` prefixes every handler path that
does not carry its own, and the new per-log `override_handler_prefix` replaces
it for one log, normalised by trimming slashes and prepending exactly one. `/`
and `/metrics` are never prefixed. Two advertised merge delays,
`max_merge_delay_sec` and `expected_merge_delay_sec`, join the config as
promises rather than enforced limits.

**Mirrors become a concept.** `is_mirror` appears; a mirror has no private key
and must carry a public one. `GetTreeHead` is withdrawn in favour of an
`STHGetter` interface, with a log-backed and a mirror-backed implementation,
and `SetUpInstance` loses its explicit client and config parameters — both now
travel inside `InstanceOptions`.

**The parsers learn two new extension families:** Subject Information Access
(timestamps and CA repositories) and RFC 3779 address and AS-number blocks.

**`gossip/minimal` arrives** with `goshawk`, `gosmin` and `x509ext` — the last
of which encodes an STH into an X.509 extension so a certificate can carry one
log's view of itself.

**`migrillian` stops being a single-log tool.** Its per-log flags —
`ct_log_uri`, `pub_key`, `trillian_uri`, `log_id`, `ct_batch_size`,
`start_index`, `end_index`, `mirror` — are replaced by a `config` file, and it
gains the master-election flags `force_master`, `etcd_servers` and
`lock_file_path`. This is the one inherited program whose command line changes
at this release, and it is graded (CLI.md §4.7).

**`certcheck` gains `system_roots`** and learns to fetch an `https://`
positional; only the flag is graded (CLI.md §4.3).

## Reading route through ct-spec/

1. **CLI.md** §2.1 — the 17 flags, `handler_prefix`, and the last note on the
   page, which is about the removal.
2. **CLIENT-API.md** §3 — the key-compliance rules and the two shapes the
   waiver takes.
3. **HTTP-API.md** §1.1 — how a path is assembled.
4. **CONFIG.md** §2.1, §2.2, §2.5 — the four new config fields, including
   the two `/metrics` gauges the merge delays are published on; then §4.1 for
   `ValidatedLogConfig`'s fields.
5. **CLIENT-API.md** §8.1, §8.2, §8.3 — `SetUpInstance`, `InstanceOptions`,
   and `STHGetter` replacing `GetTreeHead`.
6. **X509-FORK.md** §5.3 — SIA and RPKI, down to the field names, and
   **which of the two is strict about broken DER and which is not**; then
   §5.7 for the insecure curve that arrives with them, §3 and §3.1 for
   `IsFatal`, the predicate the whole non-fatal contract is now spelled with,
   and §2.2 for how a `PrintableString` with characters outside its set is
   recovered at this release.
7. **CLIENT-API.md** §6.2 — `x509ext` and `LogSTHInfo`; then **TLS-CODEC.md**
   §1 for what `Unmarshal` does with bytes left over and which callers refuse
   them.
8. **CLI.md** §4.3, §4.7, §4.10 — `certcheck`'s new `system_roots` flag;
   **`migrillian`'s rework from per-log flags to a config file**, which is the
   one inherited command line that changes here; and `goshawk`'s five flags
   against `gosmin`'s one, with the types and defaults of each.
9. **CLI.md** §2.3 — how `ct_server` ends: the signals it handles and the
   status it exits with.
10. **RELEASE-MAP.md** §7 — the milestone on one screen.

## How you will know you are done

No binary in the tree accepts the removed flag; a verifier over a
non-compliant key still refuses unless the exported variable is set; paths
assemble correctly under a global prefix and a per-log override; a mirror
config with a private key is refused and one without is accepted; SIA and
RPKI fields come out of pinned DER with the field names §5.3 gives; a P-192
certificate parses non-fatally while a bare P-192 SPKI still does not; an STH
round-trips through an X.509 extension; the merge delays show up on
`/metrics`; `ct_server` stops on a `SIGTERM` and exits 0; sixteen binaries
build, `goshawk` with five flags and `gosmin` with one, `certcheck` with
`system_roots`, and `migrillian` with `config` instead of its per-log flags.
