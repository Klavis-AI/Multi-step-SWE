# PROFILE — step 06 of 13: quota and migration (certificate-transparency-go v1.0.19)

## What this milestone adds over v1.0.17

An explicit quota API, and the beginning of log migration.

The front end has always consumed backend resources on behalf of whoever is
submitting. At v1.0.19 it says so: two new `ct_server` flags,
`--quota_remote` and `--quota_intermediate`, control whether the front end
derives a quota user from the request and from the submitted certificate's
intermediates, and the derived identity is passed on every backend call. The
consequence is an error taxonomy with a resource dimension.

`migrillian` appears: a tool that copies an existing CT log into a backend,
which means it must preserve ordering and resume from where it stopped rather
than starting over.

Four library changes ride along. `LogClient.GetEntryAndProof` fills in the last
of RFC 6962's eight endpoints — §4.8, and the last of the six reads of
HTTP-API.md §4. A `get-sth` response can convert itself with
`(*ct.GetSTHResponse).ToSignedTreeHead()`, with no client in hand.
`ctfe.RequestLog` gains an eleventh method, `IssueSCT`, and `trillian/util`
gains `BuildLogLeaf` and `ExtraDataForChain` so that the front end and
migrillian build byte-identical leaves. And `scanner.FetcherOptions` trades
`Quiet` for `Continuous`, which is what lets a fetcher follow a log that is
still growing — and what costs `scanlog` its `--quiet` flag.

`CheckLogClient` is **not** part of this milestone. It grew `BaseURI()` at
v1.0.17; if you are carrying a note to change it, that note belongs to
the step you have already done.

## Reading route through ct-spec/

1. **ARCHITECTURE.md** §2 — the backend boundary, then §2.2 for the whole of
   the quota surface: what is charged, to whom, and in what order.
2. **CLI.md** §2.1 — the two new `ct_server` flags, their types and defaults.
3. **CLIENT-API.md** §8.2 — the two new `InstanceOptions` fields that carry the
   derivations; §8.3 — `GetTreeHead`'s fifth parameter and the pointer rule.
4. **CLIENT-API.md** §8.6 — the exact certificate quota-user string.
5. **CLIENT-API.md** §8.5 — `RequestLog`, all eleven methods and their order.
6. **CLIENT-API.md** §8.7 — `BuildLogLeaf` and `ExtraDataForChain`.
7. **CLIENT-API.md** §1.2 — `GetEntryAndProof`; §3 — `ToSignedTreeHead`;
   §5.1 — `FetcherOptions`, `Continuous`, and the loss of `Quiet`.
8. **HTTP-API.md** §4 — the `get-entry-and-proof` response shape.
9. **CLI.md** §4.7 — `migrillian`'s fourteen flags; §4.1 and §3.1 — `scanlog`
   losing `--quiet` and the two `https://` default moves.

## How you will know you are done

`ct_server` accepts both quota flags and starts with them, and a submission
carries a `ChargeTo` whose `User` list holds the caller's bare IP first and one
entry per intermediate after it — and none at all when both flags are off.
`ctfe.QuotaUserForCert` reproduces the documented string exactly, prefix
included. `GetTreeHead` charges on a non-nil pointer and only then.
`RequestLog` has eleven methods and `IssueSCT` fires once per issued SCT.
`util.BuildLogLeaf` and `util.ExtraDataForChain` produce the documented bytes.
`GetEntryAndProof` and `ToSignedTreeHead` round-trip the documented wire
shapes. `FetcherOptions` has `Continuous` and no longer has `Quiet`. Fourteen
binaries build, and `migrillian` declares exactly the fourteen documented flags.
