# PROFILE — step 13 of 13: transport and tiled logs (certificate-transparency-go v1.3.3)

## What this milestone adds over v1.2.0

The last release in the series and the broadest: a dozen changes with no single
centre, spread over the front end, the parser, the log list, the storage layer
and four command lines.

**A second transport.** `--tls_certificate` and `--tls_key` together move the
frontend's own listener onto TLS 1.2 or better. Both are needed, and one alone
does nothing at all: the process starts and serves plain HTTP as before.
`--trillian_tls_ca_cert_file` does the same for the dial to the backend.
`--http_idle_timeout` overrides the server's idle timeout when positive and is
ignored when negative.

**A time dimension on admission.** A submission is *non-fresh* when the leaf's
`NotBefore` is older than `--non_fresh_submission_age` (default 24h). Non-fresh
submissions alone go through a token bucket described by
`--non_fresh_submission_limit` (`"<count>/<duration>"`, empty disables it) and
`--non_fresh_submission_burst`; over the limit is 429 with a fixed body. Fresh
submissions are never limited.

**A bound on a request.** `--max_cert_chain_size` (default 512000) caps the
body of the two write endpoints, and only those; over-size is 413.

**A second kind of log in the log list.** `loglist3` gains `TiledLog`,
`TiledLogs` on `Operator`, `AllLogListSignatureURL` and a full
parallel set of finders. A tiled log has two URLs, and the URL finder matches
either.

**A third issuance-chain backend.** PostgreSQL joins Trillian-gRPC and MySQL
behind the unchanged storage interface, with its own connection-string
validation message; the leaf-building service splits into a direct form and an
indirect one.

**A stricter parser.** The signature algorithm a certificate carries twice —
once inside the signed TBS, once outside it — must match byte for byte, or the
parse fails with `x509: mismatching signature algorithm identifiers`. A bare
TBS parse is exempt: it has only one identifier.

**Two withdrawals.** migrillian loses etcd entirely — three flags gone, and no
`--force_master` means it will not run. `internal/witness` is deleted with its
four programs — `cmd/client`, `cmd/feeder`, `cmd/witness` and
`cmd/witness/config` — taking the tree from 18 to **14** `package main`
directories.

**And the smaller surface.** `logv3_url` in the config, published by a new
per-log `/log.v3.json`; a short `Cache-Control` max-age on a short
`get-entries`; `jsonclient.Options.Authorization`;
`--continuous` on the preloader; and `--bearer_token` on `ct_hammer` — only
that one: `start_index` is older and survives unchanged (CLI.md §4.8).

## Reading route through ct-spec/

1. **RELEASE-MAP.md** §13 — the whole milestone on one screen; use it as the
   checklist, because this step is a list.
2. **CLI.md** §2.1 and §2.3 — the 31 flags, and what the new ones change about
   running the server.
3. **HTTP-API.md** §6.3 — rate limiting, body size and transport in one place.
4. **HTTP-API.md** §7 and §7.1 — `/log.v3.json`, and the bare root path.
5. **HTTP-API.md** §4.7 — the two cache lifetimes on `get-entries`.
6. **CONFIG.md** §2.6 and §2.7 — the PostgreSQL driver and `logv3_url`.
7. **CLIENT-API.md** §4.3 — `TiledLog` and the finders, field by field.
8. **CLIENT-API.md** §8.4 — storage, cache, and the service split.
9. **X509-FORK.md** §5.4 — the two identifiers and the exemption.
10. **CLI.md** §1, §4.2, §4.7 — the 14 programs, the preloader, migrillian.
11. **RELEASE-MAP.md** §14 — the absences, re-read before you stop.

## How you will know you are done

Two flags put the listener on TLS and one of them alone does not; an oversized
`add-chain` body is 413 and an oversized `get-entries` query is not; a stale
submission over the limit is 429 with the documented two lines while a fresh
one is accepted; `/log.v3.json` serves the configured URL and 404s when it is
unset; a short `get-entries` answer carries `max-age=60` and a full one
`max-age=86400`; a certificate whose outer signature algorithm was altered no
longer parses while its TBS still does; a tiled log is found by either of its
URLs; a PostgreSQL connection string selects a driver and a malformed one is
rejected by name; migrillian refuses to start without `--force_master`;
`internal/witness` is gone and 14 binaries build.
