# Step 1 — Python 3.10: retire the Python 2 compatibility layer

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as it was written when
Python 2.7 was still a supported interpreter. The plugin and the libraries
vendored under `/app/lib` (`requests`, `urllib3` — twice — `python-dateutil`,
`elasticsearch-py`, `analytics`) are single trees that had to import under both
2.7 and 3.x, so they are full of scaffolding that exists only to bridge the two.

Make it a Python 3.10 codebase. `python3.10` is the interpreter the tree must
import and run under, and the compatibility layer is gone:

1. **Nothing depends on `six`.** No module imports it by any spelling —
   `import six`, `from six import …`, `from six.moves import …`, or the
   relative `from .packages import six` the vendored libraries use — and the
   three vendored copies of `six.py` are deleted from the tree. Call sites use
   the Python 3 spelling each shim stood for; where a shim's Python 3 *value*
   is itself observable — it reaches an exception message, a repr or a return
   value — that value is part of the behaviour being preserved and comes out
   unchanged.
2. **No Python-2-only builtins.** None of `basestring`, `unicode`, `xrange`,
   `long`, `unichr`, `apply`, `buffer`, `cmp`, `coerce`, `execfile`, `intern`,
   `raw_input` or `reduce` appears as a name anywhere in the tree. (`reduce`
   and `intern` still exist on Python 3, but only behind `functools` and `sys`.)
3. **No Python-2 iteration spellings.** The calls `.iteritems()`,
   `.iterkeys()`, `.itervalues()`, `.iterlists()` and `.has_key()` are gone,
   replaced by `.items()`, `.keys()`, `.values()` and `in`, and an iterator is
   advanced with `next(x)` rather than `x.next()`. Some of the vendored classes
   *define* methods with those names; renaming or removing one is fine, and its
   callers move with it. An iterator implements the protocol once, under its
   Python 3 name: no class defines `next` **and** `__next__`, whether the
   second is written `next = __next__` or as a `next` that forwards to
   `__next__`. That pair is the two-interpreter shim, and dropping the `next`
   half of it is what makes `x.next()` unspellable here. A method that merely
   happens to be spelled `next` without being an iterator's — one whose class
   has neither `__iter__` nor `__next__`, so there is no `__next__` for it to
   be aliasing — is an ordinary Python 3 name and needs no change.
4. **No interpreter-version branching.** No `sys.version_info` test and no
   `PY2` / `PY3` flag survives anywhere, in any spelling — importing the name
   on its own with `from sys import version_info` and testing it bare is the
   same construct as reading `sys.version_info`, and goes the same way. Every
   such branch collapses to the arm Python 3 takes, and the dead arm is
   deleted rather than commented out.
5. **No legacy `__future__` imports.** `absolute_import`, `unicode_literals`,
   `print_function`, `division` and `with_statement` are unconditional
   behaviour on Python 3; importing them is noise and must go.
6. **Abstract base classes come from `collections.abc`.** Python 3.10 removed
   the aliases in `collections` itself, so neither
   `from collections import Mapping`-style imports nor `collections.Mapping`
   attribute access may remain.
7. **`gcd` comes from `math`.** `fractions.gcd` was removed in Python 3.9.
8. **The `ordered_dict` backport is deleted.** Both vendored copies of it go —
   `collections.OrderedDict` has been unconditional since 2.7.
9. **No `u'…'` string prefixes.** They are legal again on Python 3.3+ and mean
   nothing; the tree does not use them today and must not acquire any.

Throughout, *removing* something means deleting it. A branch left behind as
a block of commented-out code, or parked in a triple-quoted string, still
carries the Python 2 code and has not been removed; a one-line note recording
what a call site used to say is not a branch and is fine.

Those nine items are the whole of the structural change, and they apply
everywhere in the tree — including inside the handful of modules that cannot
be imported on this machine at all, because an optional third-party package
is absent. Those modules are in scope for all nine items exactly like any
other file. What lies outside this milestone is anything *beyond* the nine:
the tree also holds a couple of other Python 2 constructs — an implicit
relative import, an attribute Python 3 removed from `socket` — and putting
those right is not part of this step, nor is any other modernisation the
code might invite. The nine items above and the behaviour clause below are
the entire specification.

**The observable behaviour of the tree does not change.** Everything the code
does that anyone outside it can see stays exactly as it is: return values and
their types, dict ordering, the type *and the exact message text* of every
exception raised, what is written to `stdout`, `stderr` and the logging
handlers, the warnings issued, the HTTP requests the client puts on the wire
and what it makes of the responses that come back. Where a Python 2 branch and
a Python 3 branch disagreed, the Python 3 arm is the one that was already
running, so keeping its result is the whole of the job.

One class of warning is outside that clause, because it is not the tree
speaking: a `DeprecationWarning` the *interpreter* raises about a construct one
of the nine items removes. `requests/models.py` reads `collections.Callable` on
every hook registration; the `collections` ABC aliases were deprecated in 3.3
and warned on every access from 3.7 until 3.10 removed them, and that warning
is gone once item 6 is done. That is expected.
Every warning the tree raises itself — `InsecureRequestWarning`,
`SNIMissingWarning`, the rest — stays exactly as it is.

Port the vendored libraries in place. Do not upgrade them, re-vendor them from
upstream, or replace one with a dependency installed from an index: their
present behaviour is the specification, and a newer release is a different
program. Deleting a source file instead of porting it is not an answer: apart
from the compatibility shims named in items 1 and 8, every file the tree has at
the start is still there at the end.

There is no build step and nothing to start: the tree is imported as a library
under `python3.10`, with `/app/lib` on `sys.path` the way `ElasticsearchClient.py`
puts it there, and each library the plugin loads from it — `dateutil`,
`requests`, `urllib3`, `analytics`, `elasticsearch` and
`elasticsearch_connections` — imports without raising. A tree whose
libraries do not import under `python3.10` has not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration must
preserve is defined entirely by the code already in the workspace, and the
target is the public Python language, whose removals and replacements are
documented in the "What's New in Python 3.x" and "Porting to Python 3"
chapters of the standard library documentation.

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
