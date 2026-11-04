# Step 4 — Python 3.13: the retired module and the deprecated surface

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.12 codebase whose TLS path is built on the modern `ssl`
API. Make it a Python 3.13 codebase. `python3.13` is the interpreter the tree
must import and run under.

3.13 is the release that finishes PEP 594, deleting the nineteen modules that
PEP listed and retiring `lib2to3` alongside them — twenty in all, one of which
this tree imports at module scope — and the release that adds PEP 702's
`warnings.deprecated`, which is the replacement for every way this tree
currently announces, or fails to announce, its own deprecated API. Four groups
of changes.

### The retired standard-library module

Nothing may import `aifc`, `audioop`, `cgi`, `cgitb`, `chunk`, `crypt`,
`imghdr`, `lib2to3`, `mailcap`, `msilib`, `nis`, `nntplib`, `ossaudiodev`,
`pipes`, `sndhdr`, `spwd`, `sunau`, `telnetlib`, `uu` or `xdrlib`: 3.13 removed
all twenty. One of them is imported by this tree, on the first screen of
`lib/requests/utils.py`, and because that import is at module scope it is not a
latent problem — on 3.13 nothing that reaches `requests` imports at all, which
is most of the tree.

That module is used once, in `get_encoding_from_headers`, to split a
`Content-Type` header into a type and its parameters. `get_encoding_from_headers`
is the function `Response.encoding` is built on, so its answer decides how every
response body in this client is decoded. Replace the parse, so that:

1. **The header grammar answers exactly what it answers today**, for every
   input, including the ones no specification blesses. The parser being
   retired implements RFC 2045 parameters in about fifteen lines and never
   raises; whatever takes its place must return the same type, the same
   parameters and the same encoding for each of the values below, and must not
   start raising on any of them.
2. **Nothing else about that function moves.** Its signature, its return type,
   the `None` it returns when there is nothing to report, and the
   `ISO-8859-1` it returns for a `text/*` type with no charset are all part of
   the contract.
3. **The function stays where it is and stays callable on its own.**
   `requests.utils.get_encoding_from_headers` is imported and called directly,
   not only through a response, so it keeps its module, its name and its
   ability to answer a bare `{"content-type": ...}` mapping.

The grammar branches that have to survive, each of which a plausible
replacement gets wrong on its own:

* **Quoted strings.** A quoted value containing a `;` is one value, not two.
  Inside a quoted value a backslash resolves in front of a quote or another
  backslash and nowhere else: `charset="a\"b"` is the three characters
  `a"b`, and `charset="utf\-8"` keeps its backslash and is the six
  characters `utf\-8`. A general escape rule is wrong here in both
  directions. A quote that never closes, or a closing quote with no opening
  one, is not an error — the value is whatever the grammar makes of it, and it
  is the same thing it is today. An empty quoted value is an empty value, not
  a missing one.
* **Whitespace and case.** Space around the type, around the `;`, around the
  `=`, and inside a quoted value, is handled as it is now. The parameter
  *name* folds to lower case; the parameter *value* does not.
* **Degenerate values.** `''`, `'   '`, `';'`, `'; charset=utf-8'`, `'text'`,
  `'/'`, `'text/'`, `'/plain'`, a doubled or trailing `;`, and a value that is
  a `Content-Disposition` rather than a `Content-Type` all have answers today
  and keep them. Nothing may be normalised to a default type: a value this
  parser cannot make sense of is returned as it stands, and the answer
  `get_encoding_from_headers` then gives for it follows from that.
* **Parameters without a value.** `text/plain; charset` has no charset. It is
  not a charset of `''`.
* **Repeated parameters.** When the same name appears twice, the same one of
  the two wins as wins today.
* **RFC 2231 extended parameters.** `charset*=UTF-8''utf-8`,
  `charset*0=…; charset*1=…` and their relatives are *not* implemented by the
  parser being retired: it does not decode them, does not join continuations,
  and does not file them under the un-starred name. That is the behaviour to
  preserve, not the behaviour to improve on. A replacement that understands
  RFC 2231 changes the answer for these headers and is wrong here.
* **Non-ASCII text and a NUL byte** are passed through, not rejected.

and, one layer up, **through a live response**: `requests.get(...)` against a
server that sends each of those header spellings must set `Response.encoding`
to what the helper returns, leave `apparent_encoding` to the vendored charset
detector when the header says nothing, and produce the same `Response.text`.
A port that is right in the helper and wrong about where `requests` calls it
shows up only here.

There is another header parser in the standard library. It does not answer the
same way as the one being removed — on valueless parameters, on degenerate
values, on RFC 2231 and on what it substitutes for a type it cannot parse —
and each of those differences breaks the contract above on its own.
Whichever route is taken, the contract above is the contract.

### The deprecated surface

This tree documents four functions as deprecated and announces them three
different ways: two say so only in a docstring and warn nobody, two warn by
hand from inside their own bodies, and — see the next group — one reaches for a
deprecated alias of the standard library's own. PEP 702's `warnings.deprecated`
is new in 3.13 and replaces all of it.

The six module-level definitions, by name:

* `get_host` in `urllib3/util/url.py` — **both copies**
* `iter_fields` in `urllib3/filepost.py` — **both copies**
* `get_encodings_from_content` in `requests/utils.py`
* `get_unicode_from_response` in `requests/utils.py`

Mark each of them, so that:

1. **Each carries `@warnings.deprecated` with a message.** No wording is
   required; that a message exists is, because the decorator cannot be applied
   without one. `__deprecated__` on the function object is that message, which
   is the attribute PEP 702 specifies and the one a type checker reads.
2. **The warning is raised at the caller.** A hand-rolled `warnings.warn` in a
   function body blames the library's own file; the decorator blames whoever
   called it. Which file the warning names is part of the contract, and
   moving it from the library to the caller is the observable half of this
   change.
3. **Exactly one warning per call.** Adding the decorator on top of a body that
   still warns by hand announces twice. One call, one warning.
4. **No `warnings.warn(..., DeprecationWarning)` is left anywhere in the tree.**
   A deprecation is announced with the decorator, by every function that has
   one. This is about `DeprecationWarning` specifically: `InsecureRequestWarning`,
   `SecurityWarning` and `SNIMissingWarning` are this library's own contract,
   they are raised by hand on purpose, and they must not move.
5. **The functions still work.** `get_host` returns the same
   `(scheme, host, port)` for every URL shape it is given today — with a port
   and without, with a scheme and without, and for an IPv6 literal;
   `iter_fields` still yields the same pairs from a dict and from a list of
   pairs; `get_encodings_from_content` and `get_unicode_from_response` return
   what they return today. A deprecation that breaks the function is not a
   deprecation.

`requests/cookies.py` defines a `get_host` **method** on `MockRequest`. That is
the `http.cookiejar` request interface, it has nothing to do with urllib3's
function, and it is not deprecated. Leave it alone.

### The library's own callers

Marking a function that the library itself calls makes the library warn about
itself: on 3.13 the decorator raises at the caller's frame, so decorating
`get_host` and stopping there makes `urllib3` emit a `DeprecationWarning` from
`connectionpool.py` on every absolute-URL request — library code scolding its
users for something they did not write.

**Nothing inside the tree may call any of those six functions.** The documented
replacements are already in the tree: `parse_url` for `get_host`,
`iter_field_objects` for `iter_fields`. There are four call sites to move, two
in each copy of `urllib3/connectionpool.py`, and moving them must not change
what those two functions do:

* `is_same_host` still answers the same way for an absolute URL that matches
  the pool, an absolute URL that does not, and a path with no host at all.
* `connection_from_url` still returns the same pool, with the same host and
  the same port — including the default port it fills in for a URL that names
  none — and still fails the same way on a URL that has no host.
* **Ordinary traffic is silent.** `urllib3.connection_from_url(...)`, a pool's
  `urlopen("GET", <absolute url>)`, `is_same_host(...)` and a plain
  `requests.get(...)` raise **no warnings at all** — not a `DeprecationWarning`,
  not anything else. This holds for both copies of urllib3 and through
  `requests`: the warnings such a call raises are none today and must stay
  none, so one stray warning is enough to break it.

An import is not a call. `urllib3/__init__.py` and `urllib3/util/__init__.py`
re-export `get_host` by name; that is this library's public surface, and it is
not what this asks you to change.

### The deprecated logging alias

`Client._enqueue` in `lib/analytics/client.py` reports a full queue through
`Logger.warn`, the alias the standard library has been deprecating since 3.3.
It is still there on 3.13 and it still forwards, so nothing the caller gets back
changes — but the alias raises a `DeprecationWarning` from inside this tree
every time the branch is taken, and an `-X dev` run of this client is not clean
while it does.

Use the method that is not deprecated. **The log record must not move**: same
level, same logger name, same message text, so that a handler attached to the
`segment` logger sees exactly what it sees today. The branch's own contract —
what `track()` returns when the queue is full, and the queue's size afterwards
— is unchanged too.

### Both copies, always

`requests` vendors its own copy of urllib3 under
`lib/requests/packages/urllib3/`. It is byte-identical to `lib/urllib3/` today
and each copy is imported by a different half of the tree. Every change above
applies to both: two `get_host` definitions, two `iter_fields` definitions, four
call sites. Porting one and not the other is the most likely way to half-finish
this step; both copies have to be done.

### Deliberately *not* part of this milestone

* **Anything this tree invites that is not named above.** A 2016 codebase
  offers endless other modernisation, and a linter will happily point at some
  of it; none of it is part of this milestone, and some of it would change
  behaviour this step has to preserve. The target is 3.13 and the four groups of changes
  above. A module, a construct or an idiom that still imports and still runs
  under 3.13 is not this step's problem, however dated it looks. In particular,
  a function that is *not* one of the six named above does not become deprecated
  because it looks old.
* **Upgrading the vendored libraries.** Port them in place. Do not pull a
  newer urllib3, requests, elasticsearch-py, dateutil or analytics from an
  index, do not re-vendor one from upstream, and do not replace a vendored
  package with a dependency. Apart from the files this step and the previous
  ones remove, every file the tree has at the start is still there at the end.
* **Deleting the deprecated functions.** They are deprecated, not removed:
  they keep their names, their signatures and their answers, and callers
  outside this tree go on reaching them. Removing one is not a way to satisfy
  the requirement that nothing inside the tree calls it.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every exception raised, what is written to `stdout`, `stderr`
and the logging handlers, the warnings issued, the HTTP requests the client
puts on the wire and what it makes of the responses that come back.

**Where CPython 3.13 itself changed an answer, the 3.13 answer is the right
one.** The interpreter reworded some of its own error messages — what an
overlarge integer says when it will not fit a C field, what an attribute error
on a `__slots__` class says — and where a difference like that shows through,
it is correct and expected. Do not add a shim, a sort, or a compatibility
branch to reproduce what 3.12 used to say.

The previous steps' requirements still hold: nothing may reintroduce `six`, a
`__future__` import, a `sys.version_info` branch, a Python-2 builtin or
iteration spelling, a `collections` ABC alias, `fractions.gcd`, a deleted
compatibility module, `imp`, `distutils`, `asynchat`, `asyncore` or `smtpd`; no
`ssl.wrap_socket`, `match_hostname`, `CertificateError` or `PROTOCOL_SSLv*` by
any spelling, and the vendored `ssl_match_hostname` backport stays deleted; no
`datetime.utcnow()` or `utcfromtimestamp()`; the three retry sites still report
every attempt as an exception group; every module still compiles with no
`SyntaxWarning` and no `DeprecationWarning` from the compiler; and, as before, a
branch parked in a comment or in a triple-quoted string has not been removed,
and a `u'…'` prefix must not appear.

There is no build step and nothing to start: the tree is imported as a library
under `python3.13`, with `/app/lib` on `sys.path` the way `ElasticsearchClient.py`
puts it there, and each library the plugin loads from it — `dateutil`,
`requests`, `urllib3`, `analytics`, `elasticsearch` and
`elasticsearch_connections` — imports without raising. A tree whose
libraries do not import under `python3.13` has not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration must
preserve is defined entirely by the code already in the workspace, and the
target is the public Python language, whose removals are listed in "What's New
In Python 3.13" and PEP 594 and whose deprecation decorator is specified by
PEP 702.

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
