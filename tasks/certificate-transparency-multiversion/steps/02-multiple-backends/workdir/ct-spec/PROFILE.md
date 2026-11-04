# PROFILE — step 02 of 13: multiple backends (certificate-transparency-go v1.0.5)

## What this milestone adds over v1.0

One front end, many backends. At v1.0 a `ct_server` process talked
to exactly one Merkle-tree backend and the config file was a bare
`LogConfigSet`. At v1.0.5 the file may instead be a `LogMultiConfig`: a
set of named `LogBackend` entries plus a set of logs, each naming the backend
it belongs to.

The two shapes are not two code paths. A single-backend file is **converted**
into a multi-backend one on load, with a synthetic default backend, and one
validator checks the result. It makes exactly four checks and no others: a
backend name may not be empty, a backend spec may not be empty, two backends may
not share a name, and a log may not name a backend that was never declared.
Uniqueness of *log prefixes* is a later rule, arriving at v1.0.10 (CONFIG.md
section 4.4), so at v1.0.5 that check must be **absent**: a configuration
whose two logs share a prefix has to validate cleanly here. A group asserts
exactly that, so adding the later check early costs a group.

Three smaller changes ride along: the `logid` package, so that a log id is a
distinct type from a backend tree id; `jsonclient`'s request helpers now return
the raw response body alongside the parsed one, and the retry backoff is
rewritten with a multiplier and jitter; and `SetUpInstance` collapses two
parameters into an `InstanceOptions` struct.

`x509util` is the package that changes most and advertises it least. It gains
two whole files — the "possible PEM" readers in `x509util/files.go` and
`CRLToString` in `x509util/revoked.go` — respells its two `Oid…` helpers to
`OID…`, drops `OidFromNamedCurve`, and adds the two `GeneralNames` renderers.
It is an *inherited* page in TEST_INTERFACE.md §5.1 and still changes here;
§5.2 of that file lists every arrival.

## Reading route through ct-spec/

1. **CONFIG.md** §3 — single versus multi, and the conversion.
2. **CONFIG.md** §4 — the entry points and the validation rules, and which of
   them exist at v1.0.5.
3. **CLIENT-API.md** §5.5 — `logid`, the whole package.
4. **CLIENT-API.md** §1.1 — the three changed signatures and the new backoff.
5. **CLIENT-API.md** §8.1–§8.2 — `SetUpInstance` at v1.0.5.
6. **X509-FORK.md** §5.2 — the structured error type, and the two other
   `x509` files that arrive with it.
7. **CLIENT-API.md** §5.2 — `x509util` at v1.0.5: the two "possible PEM"
   readers, the `Oid…` → `OID…` respellings and the one removal, `CRLToString`,
   and the two `GeneralNames` renderers. Read the release table at the end of
   the section, not only the signature block at the top.
8. **CLI.md** §4.3 — `certcheck` and `crlcheck`, the two new programs.
9. **RELEASE-MAP.md** §2 — the same list, in one screen.

## How you will know you are done

Both config shapes load through one validator; a config naming an undefined
backend is refused and a config whose two logs share a prefix is accepted
(CONFIG.md §4 — the wording of the refusal is not graded); `go build ./...`
produces ten binaries; step 1's behaviour is unchanged.
