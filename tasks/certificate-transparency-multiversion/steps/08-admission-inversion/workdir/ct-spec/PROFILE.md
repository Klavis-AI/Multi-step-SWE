# PROFILE — step 08 of 13: admission inversion (certificate-transparency-go v1.1.0)

## What this milestone adds over v1.0.21

The largest single release in the series, and the only one where an existing
configuration file keeps its meaning in syntax and loses it in behaviour.

**The inversion.** `reject_expired` stops being a switch on path-level time
checking and becomes a leaf-only rule: reject if the leaf's `NotAfter` is in
the past, and say nothing about a certificate that is not yet valid.
`reject_unexpired` appears as its mirror. The test runs before path building;
path building then always runs with time checks disabled and an explicit
current time. Both set at once is a start-up refusal.

**More admission surface.** `reject_extensions` — a list of dotted-decimal
OIDs, any of which rejects a certificate carrying it, with an unparseable OID
a start-up failure. `frozen_sth` — a validated, signed STH that `get-sth`
serves forever while writes continue; one that does not verify under the log's
own key stops the process from starting.

**More operational surface.** `/healthz` answering `ok` unconditionally;
`--mask_internal_errors`; `--metrics_endpoint` and `--log_rpc_server` losing
their defaults, so a process with no backend configured has to say so instead
of quietly dialling `localhost:8090`. 18 `ct_server` flags, 18 programs.

**Parsing gets a documented laxness.** A `lax` struct tag and
`UnmarshalWithParams` parameter turn on exactly three relaxations, and the
`ParseCertificate` family retries with it, returning a usable certificate plus
a `NonFatalErrors` carrying the strict error.

**New packages:** `ctpolicy/`, `submission/`, `schedule/`, `loglist2/`,
`watcher/`. **Renames:** `certcheck`'s `timecheck` becomes `check_time`, and
`ignore_unknown_critical_exts` becomes `check_unknown_critical_exts` with the
polarity *and* the default reversed. **Interfaces:** `SetUpInstance` returns
an `*Instance`; `InstanceOptions` takes a validated config, an STH storage and
the mask flag; `NewCertValidationOpts` appears. `RspError` moves from `client`
to `jsonclient`, with an alias left behind, and `jsonclient.Options` gains
`UserAgent`.

## Reading route through ct-spec/

1. **HTTP-API.md** §5.2 — read this twice; it is the milestone.
2. **HTTP-API.md** §5.3 and §7 — `reject_extensions`; `/healthz`, the `/`
   catch-all and its 404 sibling, `/metrics`, and CORS.
3. **HTTP-API.md** §1 and §1.1 — the per-log path grammar, prefix
   normalisation, and the three paths `--handler_prefix` never moves.
4. **HTTP-API.md** §2 and §2.1 — the wrapper's status/body table, the
   `<status text>\n<error>\n` frame, and what `--mask_internal_errors`
   changes about a 500.
5. **HTTP-API.md** §3.1 — `add-chain`'s error rows, and the one message
   v1.1.0 prefixes with the log's own `<prefix>{<log_id>}: ` identity.
6. **CONFIG.md** §2.4 and §2.5 — the new fields and their start-up validation.
7. **CLI.md** §2.1 — the 18 flags and the two withdrawn defaults.
8. **CLI.md** §4.3 — the `certcheck` rename table, the new defaults and the
   exit-status rule the rename is graded through.
9. **X509-FORK.md** §2.1, §2.2 and §3.2 — the `lax` tag, the three
   relaxations, and the retry.
10. **CLIENT-API.md** §1.1 — `jsonclient`'s `RspError`, the `client.RspError`
    alias and `Options.UserAgent`.
11. **CLIENT-API.md** §8.1, §8.2 — `SetUpInstance` and `InstanceOptions`.
12. **CLIENT-API.md** §8.6, §8.7 — `pkix.Name.String()`'s rendering, and leaf
    construction.
13. **CLIENT-API.md** §7, §4.2 — `ctpolicy`, `submission`, `schedule`,
    `watcher` and `loglist2`: all five of the new packages.
14. **RELEASE-MAP.md** §8 — the milestone on one screen.

## How you will know you are done

An expired leaf is refused when `reject_expired` is set and an expired
*intermediate* is not; a currently-valid leaf is refused when
`reject_unexpired` is set; both set together refuses to start; a rejected
extension OID is refused by OID; a frozen STH is served verbatim and an
unverifiable one stops start-up; `/healthz` says `ok`; the strict-then-lax
parse returns a certificate and a non-fatal error; `certcheck` answers to the
new flag names; 18 binaries build.
