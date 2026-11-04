# Step 5 — A typed client surface

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.13 codebase with no retired standard-library imports and a
deprecated surface marked with PEP 702's decorator. `python3.13` is still the
interpreter the tree must import and run under, and **this step does not move
it**. Everything below is about what the code *says about itself*.

Knowing that a name is gone, an import removed or a call moved does not tell
a function that returns a `str` from one that returns `str | None`, does not
show that a handler reads a variable Python has already deleted, and does not
show that the tree uses a standard-library type in a way that type's own
declaration contradicts. Those are the defects this step is for, and the way
to make them visible is to write the types down and then check them against
each other.

Give this repository's own code a declared, checked public surface.

### The scope — this repository's code, not its vendored dependencies

The tree contains some 26,000 lines of vendored third-party libraries
(`requests`, `urllib3`, `dateutil`, `analytics`, the generated `esthrift`
bindings) and six machine-shaped Elasticsearch API tables. None of that is
in scope. **This step's scope is exactly these paths, and nothing else:**

```
ElasticsearchClient.py
events.py
commands/*.py
panel/*.py

lib/elasticsearch_connections.py
lib/elasticsearch/*.py
lib/elasticsearch/client/utils.py
lib/elasticsearch/connection/*.py
lib/elasticsearch/helpers/*.py
```

That is **158 files** — 141 in the plugin layer, 17 in the Elasticsearch
client core — carrying **382 public functions and methods**. Note what the
`lib/elasticsearch/` patterns do and do not reach: they are one directory
level each, so `lib/elasticsearch/client/__init__.py` and the five other
`@query_params` API tables under `client/` are **out** of scope while
`client/utils.py`, which defines the decorator those tables are built from, is
**in**. Annotating a file outside the scope is not required; annotating a
vendored library is a change to a dependency, and the vendored libraries are
ported in place, not rewritten.

Two files in scope are pure re-export tables — `commands/__init__.py` and
`panel/__init__.py` are lists of `from .x import Y` and define nothing. They
have no surface of their own and need no edit.

### 1. Every public function and method in scope is fully annotated

For every `def` and `async def` at **module scope or class scope** inside the
scope above, whose name does not begin with `_`, plus `__init__`, `__enter__`,
`__exit__`, `__iter__`, `__next__` and `__call__`:

* every parameter carries an annotation, including `*args` and `**kwargs`
  — but not a leading `self` or `cls`;
* the function carries a return annotation — except `__init__`, which is not
  required to.

Functions nested inside other functions are not public surface and are not
required to carry anything. There are **776 parameters and return types** to
name.

That is the rule across all 158 files. Inside the **17 files of the client
core**, requirement 6 below is stricter and it wins: a strict checker reads
every `def` and `async def` in the file, so a nested helper, a `_`-prefixed
method and a dunder that is not on the list above all have to be annotated
too, on the same terms. In the tree as it reaches you that is **18 further
definitions**, on top of the core's 57 public ones — a floor rather than a
count to hit, since a helper you factor out is one more definition the
checker reads. Nothing extra is asked of the 141 plugin
files — they are outside the strict type check, for the reason requirement 6
gives.

And the 382 are the public functions and methods the tree has as it reaches
you, by name. A public function that is gone — deleted, or renamed with a
leading underscore so that it stops being public — is not an annotated one.
That is worth saying because every other requirement on this page applies to
the definitions that are *present*, so removing a definition whose signature
is hard to type takes it out of their reach without meeting any of them.
Adding a function is fine; a migration that needs a new helper is still a
migration. Losing one is not.

### 2. Annotations are lazy

Every module in scope that carries an annotation also carries
`from __future__ import annotations`. This is what makes the whole step free
at run time: under it, an annotation is a string the interpreter stores and
never evaluates, so a name used in an annotation does not have to exist when
the module is imported, and a typing-only import can be moved behind
`if TYPE_CHECKING:` and cost nothing.

### 3. `Any` is not an annotation

No annotation in scope may be, or contain, `typing.Any` — not spelled `Any`,
not spelled `t.Any` through an aliased import, not nested inside
`list[Any]`, `dict[str, Any]` or `Callable[..., Any]`, not reached through a
type alias that resolves to it. `Any` turns the checker off for the value it
is attached to; a surface annotated `Any` is the same surface with more
punctuation.

Where a value genuinely is unconstrained, `object` says so — and it is not a
way around this requirement. Inside the client core the checker enforces the
difference: `object` will not let you call a method on it without narrowing
first, and that narrowing is the part that is worth writing down. Across all
158 files, requirement 5 enforces the other half: an `object` sitting beside
a default, or over a `return`, whose value the source spells out is as empty
a claim as `Any` and is read as one. `object` is for a value that genuinely
is unconstrained — `**kwargs: object` under a strict checker, a pooled
connection whose concrete class no stub describes — not for a value you would
rather not name.

Two places in scope that are not annotations are held to the same rule,
because they switch the checker off for a value exactly as an annotation does:
the type a `typing.cast(...)` names, and a type variable's bound or
constraints. Cast to the type the value actually has, and bound a type
variable by what it really accepts.

### 4. Nothing is suppressed

No `# type: ignore`, `# noqa`, `# mypy:` directive or `# pragma: no cover`
anywhere in scope. A suppression comment is how a typed surface stops being
one. The same goes for its spelling as code: nothing in scope is marked
`typing.no_type_check` (or `no_type_check_decorator`), under whatever name it
is imported — a checker reads nothing beneath that decorator.

Nor is anything in scope written where a checker does not look. A checker
takes `typing.TYPE_CHECKING` as true, so it never reads the branch that
constant rules out — the body of `if not TYPE_CHECKING:`, the `else:` or
`elif` of an `if TYPE_CHECKING:`, the other arm of `a if TYPE_CHECKING else b`
— nor anything after an `if TYPE_CHECKING:` block that returns, raises or
otherwise leaves. The interpreter runs exactly that code. So `TYPE_CHECKING`
(or a constant standing in for it) appears in scope only as the whole
condition of an `if` with no `else` or `elif`, and that block only declares:
imports, class and function definitions, and assignments.

### 5. An annotation has to be true of what the source already says

The scope's other requirements read presence and spelling. This one reads
meaning, against the only things that are always local and always present —
the function's own body, and the defaults written in its own signature.

**A default names its parameter's type.** Where a parameter's default is a
value the source spells out, the annotation beside it has to name that
value's type. `def f(timeout: object = 10)` is a signature contradicting
itself in six characters; so is `host: int = 'localhost'`. A true supertype
is fine — `timeout: float = 10`, `ignore: Container[int] = ()`. A default of
`None` says nothing about the non-`None` type and is not read, and neither is
a default that is a call or a name. **57** parameters in the tree as it
reaches you carry such a default; a signature you give named parameters
where it had `*args` carries more, and the rule reads whatever is there at
the end.

**And a return annotation has to be true of the body it sits on:**

* `-> None` on a body that returns a value is wrong.
* A body containing `yield` is annotated as an iterator: `Iterator[...]`,
  `Iterable[...]`, `Generator[...]` or their `Async` counterparts, however the
  name is imported. `-> list[str]` on a generator is wrong.
* A concrete, non-optional return annotation on a body that never returns a
  value is wrong.
* A return annotation that cannot be true of a returned value **the source
  spells out** is wrong. `-> int` over `return 'x'`, `-> str` over
  `return a > b`, `-> list[str]` over `return {}` — and `-> object` over any
  of them, which is the case this bullet exists for. Only values the source
  states outright are read: literals, list/dict/set/tuple displays,
  f-strings, comparisons and `not`. A `return` of a call or a name is not
  read, and neither is any annotation whose every member is a name from
  another module — an alias this rule cannot resolve is left alone rather
  than guessed at. A true supertype is always accepted: `-> Sequence[str]`
  over `return []`, `-> Mapping[str, str]` over `return {}`, `-> float`
  over `return 1`.

The third one has three exemptions, and they are exemptions rather than
oversights: an annotation that mentions `None` (`-> str | None`,
`-> Optional[str]`) or is headed `object` is true of a body that falls off the
end; a body whose only exit is `raise` declares nothing about a return value;
and a stub body — `...`, `pass`, or a bare docstring — is a declaration rather
than an implementation, which is what an abstract method and a `Protocol` are.

### 6. The client core's declarations agree with each other

The 17 files of the Elasticsearch client core listed above must pass a
**strict static type check**, run from `lib/` as the import root — which is
the directory Sublime Text puts on `sys.path`, so `elasticsearch.transport` is
how the tree names its own modules. Strict means what the standard checkers
mean by it: no implicit `Optional`, no untyped `def`, no call to an untyped
function, no returning `Any`, no unused ignore. The vendored packages are not
checked and are not followed into; neither are the `client/` API tables, apart
from `client/utils.py`.

This is the requirement the previous five cannot reach, because it is the only
one that reads a declaration against *another* declaration, and against the
standard library's own. What it finds in this tree is real and none of it is
cosmetic:

* **A Python 2 compatibility shim that survived the port off Python 2.**
  `try: import queue / except ImportError: import Queue as queue`, and
  its twin `try: from Queue import ... / except ImportError: from queue import
  ...`. It *runs* correctly on 3.13 — the Python 2 arm raises and the fallback
  takes the other — so nothing has flagged it. A checker cannot resolve a
  module that does not exist, and cannot accept a name bound twice by two
  imports. Neither complaint goes away until the shim does.
* **A `NameError` waiting in a live error path.** One handler in this tree
  reads an exception variable that an inner `except ... as e:` block has
  already unbound — Python 3 deletes that name at the end of the suite. It has
  never fired, because the path needs a dependency that is not installed. The
  fix is to stop the two handlers sharing a name; it is not to add a
  suppression.
* **A handler that is sometimes passed a group rather than a leaf.** The
  three retry sites report their attempts as exception groups, and
  `ExceptionGroup[E].exceptions` is declared as holding `E` *or another
  group* — nesting is the point of the type — so reading an attribute of `E`
  off a member, wherever the tree handles one of those groups, is not sound,
  and the declaration says so even though the transport never nests today.
* **Attributes whose type is inferred from whichever line happened to run
  first**, then contradicted later: a pool's `connections`, and two more of
  the same shape in `transport.py`. Declare them; see the constraint below.
* **A `.encode()` called on a value that is already `bytes` on one path.**
  On that path the call raises `AttributeError` today, and it still does
  after this step: what changes is the declaration, not what runs.
* **Re-export tables that do not say they are re-exporting.** Strict means no
  implicit re-export: a name a module imports is private to that module
  unless the module says otherwise, and three modules in the core exist
  precisely to re-export — `elasticsearch/__init__.py`,
  `elasticsearch/compat.py` and `elasticsearch/connection/__init__.py`. Say
  so with `__all__` and the names keep travelling; say nothing and every
  importer of them is a finding. `exceptions.py` already has its `__all__`
  and is the shape to copy. This is a declaration about names that were
  always public, not a change to what is exported — check the list against
  what `import *` reached before.

### What this step must not change: anything that runs

This is the whole risk of a typing rung, and it is worth being explicit about,
because the failure mode is a change that looks like a tidy-up and is not:

* **`cast` is not a conversion.** `typing.cast(T, v)` returns `v` untouched at
  run time. That is what makes it the right tool when the checker needs to be
  told something it cannot derive. A `str()`, an `int()` or a `bool()` added to
  make a type work *is* a conversion and is not.
* **Do not replace a test with a different test.** `if x:` and
  `isinstance(x, bytes)` disagree about `b''`. A `try: v.encode() except
  AttributeError:` and `isinstance(v, str)` disagree about every object that
  is neither. Where a type question is currently answered by running
  something, keep running it.
* **Do not narrow a signature.** Naming the parameters behind a `*args` is
  often the only way to let a checker read a `super()` call — but the names,
  the order and the defaults must be the ones the call was already reaching,
  or a call that used to bind now fails.
* **Do not split one name into two and change what the second one holds.**
  Where a name is rebound to a different type — the classic Python 2 shape,
  `body = body.decode(...)` — giving the second meaning its own name is
  correct and often necessary. Giving it a *different value* on some path is
  not, and a value that reaches a log message, a formatted string or an
  exception argument is observable.
* **Two structures are pinned by behaviour and must not be re-shaped to suit a
  declaration.** `DummyConnectionPool.connections` is a **tuple** and
  `mark_dead`, `mark_live` and `resurrect` are **one shared function object**
  bound to three names; and `ConnectionPool.connections` is a **single
  attribute mutated in place**, not replaced with a new list. Declare both;
  do not convert either.

### Deliberately *not* part of this milestone

* **A new interpreter.** This step runs on the same `python3.13` the last one
  did. Nothing here requires a feature of a newer interpreter and nothing
  should reach for one.
* **Runtime type enforcement.** No validation decorator, no `typeguard`, no
  `pydantic`, no `isinstance` check added to "back up" an annotation. The
  annotations must have no run-time effect at all.
* **Annotating the vendored libraries or the `@query_params` API tables.**
  They are out of scope, and the six tables under `lib/elasticsearch/client/`
  are close to 3,000 lines of generated-looking API surface whose parameters
  are documented in docstrings; annotating them is not this step's work.
* **Deleting a function rather than annotating it**, or moving one out of
  scope. The surface is what it is; the step is to declare it.
* **Reformatting.** A codemod that renormalises quotes, re-wraps every line or
  re-sorts every import across 158 files buries the actual change and risks
  every one of the behaviour rules above. Change the declarations.
* **Upgrading the vendored libraries.** Port in place. Do not pull a newer
  urllib3, requests, elasticsearch-py, dateutil or analytics from an index, do
  not re-vendor one from upstream, and do not replace a vendored package with
  a dependency. Apart from the files the previous steps remove, every file the
  tree has at the start is still there at the end.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every exception raised, what is written to `stdout`, `stderr`
and the logging handlers, the warnings issued, the HTTP requests the client
puts on the wire and what it makes of the responses that come back.

The previous steps' requirements still hold: nothing may reintroduce `six`, a
`__future__` import other than `annotations`, a `sys.version_info` branch, a
Python-2 builtin or iteration spelling, a `collections` ABC alias,
`fractions.gcd`, a deleted compatibility module, `imp`, `distutils`,
`asynchat`, `asyncore` or `smtpd`; no `ssl.wrap_socket`, `match_hostname`,
`CertificateError` or `PROTOCOL_SSLv*` by any spelling, and the vendored
`ssl_match_hostname` backport stays deleted; no `datetime.utcnow()` or
`utcfromtimestamp()`; nothing may import a module 3.13 retired; the six
deprecated definitions keep `@warnings.deprecated`, nothing in the tree calls
them, and no `warnings.warn(..., DeprecationWarning)` appears anywhere; the
three retry sites still report every attempt as an exception group; every
module still compiles with no `SyntaxWarning` and no `DeprecationWarning` from
the compiler; and, as before, a branch parked in a comment or in a
triple-quoted string has not been removed, and a `u'…'` prefix must not
appear.

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
target is the public Python language, whose typing surface is specified by
PEP 484 and its successors, whose lazy annotations are PEP 563, and whose
`TYPE_CHECKING` constant is documented with the `typing` module.

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
