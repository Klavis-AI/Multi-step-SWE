# Provenance of `srt-docs/`

Every upstream file below is copied byte for byte from the Haivision/srt
repository at tag `v1.3.0`. Nothing in this directory has been edited,
reordered or summarised; the column below carries the first 16 hex digits
of each file's upstream sha256 -- a prefix, enough to identify the revision.
This file itself is task-authored and is not listed, since a file cannot
state its own digest.

This step implements `v1.2.0`, which ships no `docs/` directory of its own
(only two images). Under the documentation-pinning rule this task uses,
the documents come from `v1.3.0`, the earliest tag in the chain that has any.
Read them as describing a **superset**. `PROFILE.md` §5 does not list
which part of that superset is scored -- it gives the rule that settles
the question: a default these documents state is binding on you, and a
number they do not state is not scored. The scored behaviour itself is
enumerated in `TEST_INTERFACE.md` §3.

| sha256 | file |
|---|---|
| `4adbe333e7b0bc70` | `API.md` |
| `3c140773bc3f4c0b` | `encryption.md` |
| `718aaaa1f22520ae` | `live-streaming.md` |
| `8cb94de7e1695e9e` | `why-srt-was-created.md` |
