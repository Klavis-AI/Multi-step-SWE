# Step 3 — Python 3.12: rebuild the TLS path on the modern `ssl` API

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.11 codebase whose retry paths report every attempt. Make it
a Python 3.12 codebase. `python3.12` is the interpreter the tree must import
and run under.

3.12 is the release that deletes things this tree still uses, and the first one
under which its HTTPS support does not work at all. Four groups of changes.

### The vendored TLS layer

`lib/urllib3/util/ssl_.py` begins by importing three names from `ssl` on one
statement, one of which — `wrap_socket` — 3.12 removed. Because the statement
is atomic and the `except ImportError` around it is a bare `pass`, losing that
one name loses all three, the module still imports cleanly, and the first thing
to resolve a certificate requirement dies with a `NameError` a long way from
the cause. Everything in this tree that speaks HTTPS goes through that file.

Two more names are used from `lib/urllib3/connection.py` and
`lib/urllib3/connectionpool.py`: `ssl.match_hostname`, which 3.12 removed, and
`ssl.CertificateError`, the exception it raises, which since 3.7 is only a
legacy alias of `ssl.SSLCertVerificationError`. The tree reaches both through
its own vendored backport at `lib/urllib3/packages/ssl_match_hostname/`. The backport dates from
a time when the standard library could not check a hostname itself. It can, and
has been able to since 3.4, from inside the handshake.

Move the library onto the `ssl` API 3.12 actually has, so that:

1. **Nothing references a removed, refused or legacy-alias `ssl` name.** No `ssl.wrap_socket`,
   and no `match_hostname`, `CertificateError`, `PROTOCOL_SSLv2`,
   `PROTOCOL_SSLv3` or `PROTOCOL_SSLv23` by any spelling — not imported, not
   called, not re-vendored under another name, not caught in an `except`
   clause. `SSLContext.wrap_socket` is the replacement for the removed
   function and is not one of these.
2. **The vendored backport is deleted**, not left unused: the
   `packages/ssl_match_hostname` directory and its modules are gone from both
   copies of urllib3, and nothing imports them.
3. **`PROTOCOL_SSLv23` is no exception just because it still resolves.** It
   is an alias of `PROTOCOL_TLS` and 3.12 answers to it; it still has to go
   under (1). Constructing an `SSLContext` from either spelling has warned
   since 3.10, and this library is a client and only a client.

and so that **all of the following stay exactly as they are today**:

* **A verified request succeeds and an unverifiable one fails.** With
  `cert_reqs="CERT_REQUIRED"` and a `ca_certs` file naming the issuer, a GET
  against a matching certificate returns its status and its body. Against a
  certificate whose names do not match the host, or one issued by a CA the
  file does not contain, the call fails — and it fails *before anything is
  written to the socket*: the server must see no request at all. With
  `cert_reqs="CERT_NONE"` all three of those connect and return the body.
* **Failures stay inside this library's own exception hierarchy.** A
  connection this library refuses raises one of its own exceptions —
  `urllib3.exceptions.HTTPError` and, where the tree already raises it,
  `elasticsearch.exceptions.SSLError`. A bare `ssl.SSLError`,
  `ssl.SSLCertVerificationError` or `OSError` escaping to the caller is a
  regression even when the connection was correctly refused. That includes the
  error a bad `ca_certs` path produces: `util/ssl_.ssl_wrap_socket` translates
  a CA file that does not exist, or that is a directory, into this library's
  `SSLError`, and it must go on doing so.
* **`assert_hostname` and `assert_fingerprint` keep working.**
  `VerifiedHTTPSConnection` lets a caller name a *different* hostname to
  verify than the one dialled, switch the hostname check off with
  `assert_hostname=False`, and pin a certificate by fingerprint instead. All
  three still mean what they mean now: the named host is the one verified, a
  fingerprint pin is what decides the connection when one is given, and
  waiving the hostname check waives *only* the hostname check — a certificate
  from an issuer the pool does not trust is still refused.
* **Client certificates are still sent.** A pool built with `cert_file` and
  `key_file` presents that certificate to a server that asks for one. The
  server-side view is what matters here: a client that quietly stops sending
  its certificate to a server that accepts connections either way still gets
  its `200`, and only the peer it authenticated as shows the difference.
* **The knobs answer what they answer now.** `resolve_cert_reqs` and
  `resolve_ssl_version` map the same inputs to the same values, including
  `None`; `create_urllib3_context()` returns a context with the same
  `verify_mode` and the same `OP_NO_SSLv2` / `OP_NO_SSLv3` /
  `OP_NO_COMPRESSION` options set; `util.ssl_` still binds `HAS_SNI` true, a
  `SSLContext` that is the standard library's own class, a
  `create_urllib3_context`, and the same `DEFAULT_CIPHERS` string. This
  library shipped a hand-rolled `SSLContext` shim for interpreters that had
  none, which warns and cannot do SNI; falling back to it is not a migration.
* **The warnings do not move.** `InsecureRequestWarning` when a request is made
  without verification, and the `UserWarning` the elasticsearch connection
  classes raise for `verify_certs=False` — same warnings, same categories, same
  text, in the same situations. `SecurityWarning` keeps its name and its place
  in the exception hierarchy; the situation that raises it today — a verified
  certificate carrying no `subjectAltName` — is not a requirement either way,
  because the hostname matchers this tree can end up using disagree about what such a
  certificate means, and a correct migration is entitled to change that answer.
* **The elasticsearch layer above is unchanged.** `Urllib3HttpConnection` and
  `RequestsHttpConnection`, `Elasticsearch(...).info()` over HTTPS, and every
  error class the transport maps a TLS failure onto behave as they do now.

### The retired standard-library module

Nothing may import `imp`, `distutils`, `asynchat`, `asyncore` or `smtpd`:
3.12 removed all five. One of them is imported by this tree.

### The naive UTC clocks

`datetime.utcnow()` and `datetime.utcfromtimestamp()` are deprecated at 3.12.
Neither may appear anywhere in the tree. The instants they produced must not
move: each call site keeps the moment in time it had, and a site that went on
to attach a UTC offset keeps the offset it attached. Read each one — they are
not all the same shape, and at least one is not a clock reading at all.

Deprecated means it warns. Two of those call sites are in `requests/sessions.py`
and run on every single request, so on 3.12 an ordinary `requests.get(...)`
followed by `.text` and `.json()` raises a `DeprecationWarning` from inside the
tree where on 3.11 it raised nothing. Finishing this item is what makes that
request quiet again; leaving one call site behind is visible from outside the
library even though nothing it returns has changed.

### The compiler's own complaints

Every module must compile under 3.12 with no `SyntaxWarning` and no
`DeprecationWarning` from the compiler. This tree has invalid escape sequences
in string literals — 3.12 is the release that promoted them from a
`DeprecationWarning` to a `SyntaxWarning`, and they are a future `SyntaxError`.
Fixing one must not change what the literal evaluates to: almost every one of
them is a docstring, and deleting the backslash instead of escaping it or
making the string raw rewrites the text a vendored library ships.

### Both copies, always

`requests` vendors its own copy of urllib3 under
`lib/requests/packages/urllib3/`. It is byte-identical to `lib/urllib3/` today
and each copy is imported by a different half of the tree. Every change above
applies to both. Porting one and not the other is the most likely way to
half-finish this step; both copies have to be done.

### Deliberately *not* part of this milestone

* **Anything this tree invites that is not named above.** A 2016 codebase
  offers endless other modernisation, and some of it is even flagged by the
  tools; none of it is part of this milestone, and some of it would change
  behaviour this step has to preserve. The target is 3.12 and the four groups of changes
  above. A module, a construct or an idiom that still imports and still runs
  under 3.12 is not this step's problem, however dated it looks.
* **Upgrading the vendored libraries.** Port them in place. Do not pull a
  newer urllib3, requests, elasticsearch-py, dateutil or analytics from an
  index, do not re-vendor one from upstream, and do not replace a vendored
  package with a dependency. Apart from the files this step and the previous
  ones remove, every file the tree has at the start is still there at the end.
* **Loosening the security posture to make a handshake succeed.** Turning
  verification off where it was on, widening a cipher list, or accepting a
  certificate the tree rejects today is not a port of this code.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every exception raised, what is written to `stdout`, `stderr`
and the logging handlers, the warnings issued, the HTTP requests the client
puts on the wire and what it makes of the responses that come back.

**Where CPython 3.12 itself changed an answer, the 3.12 answer is the right
one.** The interpreter reworded some of its own error messages and moved a
default or two; where a difference like that shows through, it is correct and
expected. Do not add a shim, a sort, or a compatibility branch to reproduce
what 3.11 used to say.

The previous steps' requirements still hold: nothing may reintroduce `six`, a
`__future__` import, a `sys.version_info` branch, a Python-2 builtin or
iteration spelling, a `collections` ABC alias, `fractions.gcd`, or a deleted
compatibility module; the three retry sites still report every attempt as an
exception group; and, as before, a branch parked in a comment or in a
triple-quoted string has not been removed, and a `u'…'` prefix must not appear.

There is no build step and nothing to start: the tree is imported as a library
under `python3.12`, with `/app/lib` on `sys.path` the way `ElasticsearchClient.py`
puts it there, and each library the plugin loads from it — `dateutil`,
`requests`, `urllib3`, `analytics`, `elasticsearch` and
`elasticsearch_connections` — imports without raising. A tree whose
libraries do not import under `python3.12` has not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration must
preserve is defined entirely by the code already in the workspace, and the
target is the public Python language, whose `ssl` API is documented in the
standard library reference and whose removals are listed in "What's New In
Python 3.12".

**Additional files:** `/app/README.md` describes what the plugin does and how
its pieces fit together; `/app/timer.sh` reports your remaining budget.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests and documents. Do not create a prose answer, a summary of what you
  would have done, or an alternate entry point in place of the edits
  themselves.
- Do not cheat by using online solutions or hints
  specific to this task, and do not search for, download, copy, wrap or invoke
  existing solutions or answers of any kind.
- Leave the workspace in a state a fresh session can pick up: the tree as it
  stands is the whole of the deliverable.
