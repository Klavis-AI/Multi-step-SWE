# PROFILE — step 10 of 13: client rebuild and witness (certificate-transparency-go v1.1.3)

## What this milestone adds over v1.1.2

The release where a tool's argument grammar is replaced wholesale, a type
changes package without changing content, and a service is added beside the log
rather than inside it.

**The client rebuild.** `ctclient` stops being one `flag`-package `main` with a
trailing command word and becomes a root command over `client/ctclient/cmd/`:
subcommands with their own flag sets, five persistent connection flags that
bind on either side of the command word, hyphenated canonical names with the
old spellings kept as aliases, `size` and `prev_size` retyped from Int64 to
Uint64 with their default moving from `-1` to `0`, the logging library's flags
folded into the new flag set, no subcommand accepting a positional, and every
parse error routed through `Fatal` so the exit status is 255 rather than 1
(CLI.md §3.1 against §3.2).

**A type moves.** `ctfe.PEMCertPool` becomes `x509util.PEMCertPool`, unchanged
in content, to break a dependency from a library package onto the personality.
`CertValidationOpts`'s root pool and the v2 log-list `LogRoots` map retype with
it, and `ctfe.PEMCertPool` ceases to exist.

**A third log-list schema.** `loglist3/` is born: the v2 shape plus exactly two
things, a list version with a publication timestamp and per-log
previous-operator history (CLIENT-API.md §4.3). Everything else it carries is
v2's and unchanged — operator contact details, the six-member state union with
its seven `LogStatus` constants, the finders and the selection helpers — and
the one field that is *not* here yet is `IsAllLogs`, which arrives at v1.2.0.
Nothing in the product consumes the package yet except the new witness client,
so this is the one release at which all three log-list packages are present.

**A witness.** `internal/witness/` arrives: a small HTTP service that holds the
newest tree head it has seen for each configured log, verifies a caller's newer
one for consistency against it, stores that and returns it co-signed, and
refuses what it cannot reconcile. Four programs come with it.

**Two withdrawals and three smaller changes.** `ingestor/` and `watcher/` are
deleted for having no callers. `PostAndParseWithRetry` moves 429 onto the same
arm as 503. `HammerCTLog` gains a leading context and stops when it is
cancelled. `LogConfig.is_readonly` (field 19) makes a log serve reads and
answer 404 on the two write paths. 19 programs, 19 `ct_server` flags.

## Reading route through ct-spec/

1. **CLI.md** §3.1 and §3.2 — both halves, in that order. The break between
   them is the milestone.
2. **CLIENT-API.md** §5.3 — where `PEMCertPool` lives, then §8.3 for the
   caller whose field and constructor retype.
3. **CLIENT-API.md** §4.3, and §4.2 for the neighbour it is defined against.
4. **CLIENT-API.md** §6.3 — the witness: its paths, its request and response
   types, its verifier.
5. **CLI.md** §4.10 for the witness programs and their flags, §1 for the
   inventory they join.
6. **CLIENT-API.md** §1.1 — the retry state machine, with 429 in its new arm.
7. **HTTP-API.md** §6.2 with **CONFIG.md** §2.5 and §1 — `is_readonly`: what
   it removes, and the field number that makes it observable.
8. **CLI.md** §4.8 — `HammerCTLog`'s new first parameter.
9. **MERKLE.md** §4.2 — which proof library is in force at this release.
10. **RELEASE-MAP.md** §10, then §14 — the milestone on one screen, and the
    directory listing that proves you are at it.

## How you will know you are done

`ctclient --help` lists subcommands instead of flags; each subcommand answers
to its canonical name and to every alias; a flag written after the command word
is parsed rather than swallowed; an unknown command exits 255. `PEMCertPool`
resolves in `x509util` and nowhere else. A pinned v3 log list decodes field for
field and verifies under a detached signature. The witness accepts a first tree
head, co-signs it, refuses one that goes backwards or cannot be proved
consistent, and accepts the next one that can. A POST answered 429 is retried
instead of returned. A log configured read-only serves `get-sth`, `get-entries`
and `get-roots` and answers 404 on both write paths. `ingestor/` and `watcher/`
are gone. 19 binaries build.
