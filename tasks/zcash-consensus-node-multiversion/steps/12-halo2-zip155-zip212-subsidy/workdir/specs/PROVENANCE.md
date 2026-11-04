# Normative corpus — provenance and pins

Everything in this directory is shipped verbatim and is immutable. Do not edit
it; the grader reads the same bytes.

| Artifact | Source | Pin |
| --- | --- | --- |
| `nu5.pdf` | <https://zips.z.cash/protocol/nu5.pdf> | version `v2026.7.0-191-g0fae78 [NU5]`, dated 2026-09-15. SHA-256 `9f87b82f365a0fb1a1f1090e62400f8189b046acedba0dc056b30d822845ee02`, 3,354,960 bytes |
| `nu5.txt` | `pdftotext -layout nu5.pdf` | derived mechanically from the PDF above; provided because the PDF's mathematical notation does not survive plain-text extraction cleanly and the PDF itself is the authority wherever the two differ |
| `zips/zip-*.rst` | <https://github.com/zcash/zips>, MIT | repository commit `0fae783d424039a09759a69ff993317ded72b97d`, 2026-09-15 — the same commit that produced the specification PDF above |

`nu5.pdf` is the **Zcash Protocol Specification, NU5 edition**. It is the single
normative document for the consensus protocol. Section numbers cited anywhere in
this task refer to that edition.

The ZIPs are shipped as their reStructuredText sources rather than as rendered
HTML so that they are greppable. Each is the Final or Active text at the pinned
commit.

Copyright in the specification and the ZIPs is held by their authors and the
Electric Coin Company; both are distributed under the MIT licence, a copy of
which is in `zips/`.
