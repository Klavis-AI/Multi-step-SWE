# ct-spec — the documentation corpus for this benchmark

This directory is the **only** description of the product you are building.
There is no upstream source tree in this container, no network, and no
reference implementation to copy from. Everything here describes *behaviour*
and *wire formats*: what must be on the wire, what must be in a struct, which
error must come back, and at which release each of those changed.

Sixteen files: eleven prose pages (including this index), one `PROFILE.md`
rewritten for the step you are on, three IETF RFCs shipped verbatim, and the
`SHA256SUMS` manifest over all of them. Roughly **three quarters of a megabyte**
in total; the three RFCs are a little over half of it.

---

## Where to start

If you read three things, read `PROFILE.md`, then the page it sends you to
first, then `RELEASE-MAP.md` for the release you are on.

| Read | For |
| --- | --- |
| `PROFILE.md` | **start here.** What this step's milestone adds over the previous one, a reading route through these pages, and how you will know you are done. Rewritten for every step. |
| `ARCHITECTURE.md` | the shape of the whole product: the library stack, the personality, the backend boundary, the process and flag boundary, and how a request travels through them |
| `RELEASE-MAP.md` | what changed at each of the thirteen releases, one section per release, plus structural facts you can check against your own tree |
| `TLS-CODEC.md` | the TLS presentation-language codec and the struct tags that drive it; the RFC 6962 structures and their exact encodings |
| `X509-FORK.md` | why `asn1/` and `x509/` are forked: the `lax` relaxations, `NonFatalErrors`, precertificates and the poison extension, SIA and RPKI |
| `MERKLE.md` | the three hashing rules, the two proof algorithms, and which release owns the verifier |
| `HTTP-API.md` | the eight RFC 6962 endpoints plus the ones outside it: paths, parameters, status codes, error bodies, admission control, and the gRPC-to-HTTP error map |
| `CONFIG.md` | the log configuration: every message, every field, when each arrived, how the file is parsed and exactly which validation errors it can produce |
| `CLI.md` | every `package main` in the tree: which programs exist at which release, their full flag sets with types and defaults, start-up order and exit statuses |
| `CLIENT-API.md` | the importable Go surface — `client`, `jsonclient`, `ctutil`, the log-list packages, `scanner`, `x509util`, `fixchain`, and the `trillian/ctfe` entry points |
| `GLOSSARY.md` | the vocabulary, with the filesystem path each term corresponds to. Read it once early; it is short and it prevents a whole class of misreading. |
| `rfc/rfc6962.txt` | **normative.** Certificate Transparency: the log's data structures, the eight endpoints, the proofs. |
| `rfc/rfc5246.txt` | TLS 1.2. §4 is the presentation language every structure in this product is written in. |
| `rfc/rfc6979.txt` | deterministic ECDSA — background for why a signature over the same input may or may not repeat |

`SHA256SUMS` lists every file in this directory with its digest. If you think
you have damaged a page, `cd /app/ct-spec && sha256sum -c SHA256SUMS` will tell
you which one.

---

## The RFCs

Fetched verbatim from the RFC Editor and shipped unmodified.

| file | URL | bytes | sha256 |
| --- | --- | ---: | --- |
| `rfc/rfc6962.txt` | `https://www.rfc-editor.org/rfc/rfc6962.txt` | 55048 | `08fdf31c10b9f20872a65c027a9fe8cee27280f930120784241a57de744752af` |
| `rfc/rfc5246.txt` | `https://www.rfc-editor.org/rfc/rfc5246.txt` | 222395 | `2ebe1b8e651696aeacc8ab3848bedb465505737901ea3b691acf40c392958feb` |
| `rfc/rfc6979.txt` | `https://www.rfc-editor.org/rfc/rfc6979.txt` | 140386 | `456e8f17558fdbd206f968b96fc6f1b4a71ea331ab30ad17f711ab3adaa7d701` |

Where an RFC and a page here disagree, the RFC wins on the wire format and the
page wins on what this particular product does with it — the product has
documented deviations, and where it has one, the page says so.

---

## How to read these pages

* **Section numbers are stable** and are cited from `PROFILE.md`, from
  `TEST_INTERFACE.md` and from the step instruction. A reference like
  `HTTP-API.md §5.2` will keep pointing at the same material for the whole
  series.
* **A release column means the behaviour changed there.** Several tables give
  a behaviour per release; the row for the release you are on is the one that
  binds, and the rows before it are what earlier steps built and what is still
  checked.
* **Message text in backticks is part of the contract** wherever a page says
  so. Format verbs (`%v`, `%d`) appear as they do in the product.
* **These pages are not source code.** No page contains a pasteable
  implementation. Where a signature is given, it is given because callers
  depend on it, not as a suggestion about how to write the body.
