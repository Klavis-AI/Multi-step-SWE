# PROFILE — step 04 of 13: ctutil verification (certificate-transparency-go v1.0.13)

## What this milestone adds over v1.0.10

`ctutil`: verification as an importable surface, with known answers.

Until now, checking an SCT was something the product did inside its own
programs. At v1.0.13 it becomes a package a third party imports:
`LeafHash`, `LeafHashB64`, `VerifySCT`, `ContainsSCT`, and a `LogInfo` type
that carries a log's description, key, merge delay and last-seen tree head and
can answer "is this leaf in that log, and at what index".

Two companions arrive with it. `loglist` parses the published catalogue of
known logs (schema v1) so that a verifier can find the log an SCT names.
`dnsclient` answers the read-side queries over DNS TXT records instead of
HTTPS, so an auditor need not open an attributable connection to the log they
are asking about.

`client.CheckLogClient` appears at the same time: a three-method read-only
interface so `LogInfo` can be driven by something other than a real HTTP
client.

## Reading route through ct-spec/

1. **CLIENT-API.md** §2 — the whole `ctutil` surface, including the `embedded`
   flag's effect on the hashed pre-image and the `-1` failure convention.
2. **X509-FORK.md** §4.2 and §4.3 — the precertificate rebuild that
   `embedded` depends on: `RemoveSCTList` is the one the verification side
   uses, `RemoveCTPoison`/`BuildPrecertTBS` the one the submission side uses.
3. **MERKLE.md** §1–§2 — the hashing rules and inclusion verification.
4. **CLIENT-API.md** §4.1 — log list schema v1, field by field.
5. **CLIENT-API.md** §6.1 — `dnsclient`: what `New` does with a key, and what
   it leaves on the `DNSClient` for a caller to see. The read methods and
   their four query shapes are on the same page and are part of the milestone
   — write them — but section 9 of TEST_INTERFACE.md explains why nothing at
   this step runs them.
6. **CLIENT-API.md** §1.3 — `CheckLogClient` at this release, three methods.
7. **CLI.md** §4.4 and §4.5 — `sctcheck`, `sctscan`, `findlog`.
8. **CLIENT-API.md** §5.2 and **X509-FORK.md** §4.2, §4.4 — the `x509util`
   and `x509` helpers `sctcheck` and `ctutil` are built out of.

## How you will know you are done

`ctutil.LeafHash` matches the pinned vector for all three shapes it has to
cover — a plain certificate, a precertificate, and a certificate carrying the
SCT inside it; `VerifySCT` accepts a genuine SCT and rejects a tampered one; every `VerifyInclusion*` returns -1 with its error; a fixture log list
parses and every finder works; thirteen binaries build.
