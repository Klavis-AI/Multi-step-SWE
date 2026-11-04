# Step 7 — Python 3.14: annotations that are objects, and that resolve

## 1. Implement this milestone

`/app` holds the **sublime-elasticsearch-client** plugin as the previous step
left it: a Python 3.13 codebase with a typed client core that is correct with
the GIL switched off. Make it a Python 3.14 codebase. `python3.14` is the
interpreter the tree must import and run under, and it is the last rung of
this migration.

Nothing in this tree stops parsing on 3.14 and no module it imports has been
removed. What 3.14 changes is *when* an annotation is evaluated, and that
turns two things about the way this tree's annotations are written into
defects.

Until 3.14, an annotation was evaluated eagerly — at the moment the `def` or
the class body executed — unless `from __future__ import annotations` was in
force, in which case it was never evaluated at all and `__annotations__` held
the source text of the expression as a string. This tree's type surface was
written under that second regime: the modules that carry annotations carry
the import that makes each of them a string.

3.14 implements PEP 649 and PEP 749. An annotation is now compiled into a
function stored on the object and run the *first time something reads it* —
`__annotations__`, `typing.get_type_hints`, `dataclasses.fields`,
`inspect.signature(..., eval_str=True)`, a documentation generator, a
serialisation library, a dependency injector. The expression is evaluated in
the defining module's own globals, at that moment. Forward references and
mutually recursive types work without quoting, and without the future import.

Two consequences, and this step is both of them, plus one unrelated thing the
same release forces, plus the clean-up a last rung owes the whole tree.

### The future import has to go

`from __future__ import annotations` is still honoured on 3.14 and still
selects PEP 563 — the stringised behaviour, which PEP 749 schedules for
deprecation and which no caller can opt out of. While it is present, PEP 649
does nothing: the annotations stay text.

1. **No module imports `annotations` from `__future__`.** Every module that
   carries the import today loses it.
2. **Every annotation on the client core's public surface is an object.**
   Reading `__annotations__` off a public class, method or function of the
   sixteen `elasticsearch` modules of the client core — the sixteen files the
   core's scope, restated below, names under `lib/elasticsearch/`, the
   package's own `__init__.py` included — returns types, not strings, and
   raises nothing.
3. **Nothing is quoted instead.** Writing `def f(c: "Elasticsearch")` puts the
   string back one annotation at a time, and the string is what this step is
   removing. Nowhere in the tree is a type named as a string — not a
   parameter's, not a return's, not an annotated assignment's, and not one
   nested inside a subscript. The future import made quoting
   pointless rather than wrong; from this step on it is wrong.

### Every name an annotation is built from has to exist at run time

This is the half that deleting an import does not give you, and it is the
larger half of the work.

A `if TYPE_CHECKING:` block does not run. Under PEP 563 that was fine, because
nothing ever evaluated the annotation that used the name; under PEP 649 the
first reader gets a `NameError`, from a module that imported cleanly, at
whatever point in the program's life something first looked at a signature.
The tree's module-level blocks — in the vendored `lib/elasticsearch/` client
and in the plugin's own modules — hold the imports the type surface reads its
names from, along with whatever `Protocol`, `TypedDict` and PEP 695 `type`
declarations it was written with.

1. **No module-level `if TYPE_CHECKING:` block holds an import.** Wherever the
   block is: at column zero, or nested inside a `try:`, an `else:` or a
   version check, anything that runs when the module is imported.
2. **`typing.get_type_hints` resolves every annotation on the public surface
   of those sixteen modules**, and raises on none of them.
3. **Every name every annotation in the tree is built from is bound by
   something that runs**, not only the ones on that public surface. The
   plugin's modules are annotated too, and their blocks count exactly as the
   client's do. Moving an import out of a block satisfies this. Deleting it
   along with the block does not, and nor does parking it anywhere else the interpreter
   does not reach.
4. **The declarations in those blocks are part of this**, not just the
   imports. A `Protocol`, a `TypedDict` or a `type X = ...` alias declared
   inside a block that does not run is a name that does not exist, and an
   annotation naming it fails exactly like an annotation naming an unimported
   module. Every protocol, `TypedDict` and `type` alias declared inside one
   of those blocks is in that position, and each one has to end up somewhere
   that runs.
5. **Not every one of these imports can simply be hoisted.** Moving an import
   out of a block changes *when* it runs — from never to module-import time —
   and an import that only stayed legal because it never ran can close a
   cycle. Hoisting such an import turns a working library into an
   `ImportError` at `import elasticsearch`. Every rule above still holds for
   it: it does not stay in the block, it is not
   quoted, and the name its annotation is built from is bound in the module's
   own globals by the time anything reads that annotation — which is a place a
   name bound inside a function body never reaches.
6. **A block that is not at module level stays where it is.** Requirement 1
   is about module-level blocks, and a `TYPE_CHECKING` block in a class body
   is a different animal: it can declare a method the class states but has
   never acquired at run time, so that a base says what its subclasses provide
   without growing a callable of its own. Such a block binds nothing, is
   evaluated by nothing, and is not an import. Lifting one would add a method
   to that class's run-time surface, which is a change to the library rather
   than to how its annotations are stored.
7. **`TYPE_CHECKING` itself is not banned.** It stays importable, and a file
   that still has a use for the name once all of the above is done may keep
   its import of it.

### `return` in a `finally` (PEP 765)

Unrelated to annotations, and the same release. 3.14 warns at compile time
about `return`, `break` or `continue` leaving a `finally:` block, because such
a statement discards whatever was propagating at the time — including an
exception the surrounding `except` deliberately did not catch. This tree has
exactly one, in `Consumer.upload` in `lib/analytics/consumer.py`.

1. **No `return`, `break` or `continue` leaves a `finally:` block**, and every
   module still compiles with no `SyntaxWarning`.
2. **What the `finally` does still happens.** Every item in the batch is still
   marked done, on every path, including the paths that raise.
3. **`upload` still returns what it returns today** for both of its ordinary
   outcomes: `True` when the batch was sent, `False` when `request` raised
   something the `except Exception` catches, and `False` immediately when the
   batch is empty. The `on_error` callback is still called with the exception
   and the batch.
4. **A `BaseException` raised by the send now leaves the method.** That is the
   point of the warning: today, when `request` raises something the
   `except Exception` does not catch — a `KeyboardInterrupt` or a
   `SystemExit` — the `finally` still runs, its `return` discards that
   exception, and `upload` returns `False` instead. This is the one
   observable behaviour this step is *expected* to change, and it must change
   in this direction only — the `except Exception` clause still catches
   exactly what it catches today, which is the other half of the same
   requirement, and an exception raised while the batch is being marked done
   still leaves the method, as it does today.

### The tree is written in Python 3.14, not merely accepted by it

This is the last rung, and the code it leaves behind reads as code written for
the interpreter it runs on. Across **every** Python file in `/app` — the
plugin, every vendored library, their test modules — no spelling survives
that only an older Python needed, and none of the patterns that are well
known to hide a bug does either. The nine items below are the whole of that
requirement: most name a construct this tree still carries somewhere, the rest
name what it must not acquire on the way, and in each the replacement is the
one the language documents today.

1. **Source files.** Every `.py` file is UTF-8 and says nothing about it: no
   `# -*- coding: … -*-` declaration of any encoding, and a file written in
   another encoding is re-encoded (its strings keep their values). No file
   opens with a blank line above its first statement, comment or docstring,
   and a file that holds nothing but blank lines is empty.
2. **String formatting.** No `%`-formatting of a string literal
   (`'%s=%d' % (k, v)`); use an f-string, or `str.format` with automatic `{}`
   fields — and where a
   `str.format` call only interpolates plain names, attributes or simple
   expressions, the f-string it stands for. No explicitly numbered fields in
   order (`'{0} {1}'`). The text produced does not change: `%d` truncated a
   float and `{:d}` refuses one, so say what `%d` did where a float can reach
   it.
3. **Classes.** No explicit `object` base and no empty `()` after a class
   name; no `metaclass=type` and no `__metaclass__ = type`; `super()` takes
   no arguments where they would only name the method's own class and its
   first parameter; `collections.namedtuple` becomes a
   `typing.NamedTuple` class, a functional `NamedTuple(...)`/`TypedDict(...)`
   becomes class syntax, and a `class X(str, Enum)` is a `StrEnum`; a class
   body that has other statements carries no `...` among them.
4. **Exception names.** `IOError`, `EnvironmentError`, `WindowsError`,
   `os.error`, `socket.error`, `select.error` and `mmap.error` are spelled
   `OSError`; `socket.timeout` and `asyncio.TimeoutError` are spelled
   `TimeoutError` — under whatever name they were imported as, wherever one
   is caught, raised or constructed.
5. **Built-ins and the standard library.** No `from builtins import …` and no
   `io.open`; no call of `str`, `bytes`, `int`, `float`, `bool` or `complex`
   that has no argument or whose argument is a literal of that same type
   (`str('…')`, `bytes(b'…')`, `bytes()` for `b''`, `int()` for `0`); no
   `type()` of a string, bytes, number or boolean literal standing for its
   type (`type('')` for `str`, `type(0)` for `int`); no string literal
   encoded to get bytes a bytes literal can spell — with the default codec
   or `'utf-8'`, a literal whose characters are all ASCII is written
   `b'…'` and any other literal or f-string leaves the codec out
   (`'é'.encode()`), and with `'ascii'` or `'latin-1'` a literal written
   with no `\u`, `\U` or `\N` escape (and, for `'ascii'`, no `\x` escape)
   is written `b'…'` too;
   no doubled parentheses such as `print((x))`; `open()` is not
   passed its default mode (`'r'`, `'rt'`); `datetime.UTC`, not
   `datetime.timezone.utc`; `for x in it: yield x` is `yield from it`;
   `subprocess` calls say `capture_output=` and `text=` rather than
   `stdout=PIPE, stderr=PIPE` or `universal_newlines=`; `functools.cache`
   rather than `lru_cache(maxsize=None)`, and `@lru_cache` without empty
   parentheses; `unittest`'s current method names, not the deprecated
   aliases; `unittest.mock`, not `mock`; `xml.etree.ElementTree`, not
   `cElementTree`; `shlex.join(xs)`, not `' '.join(shlex.quote(x) for x in
   xs)`; no `__future__` import of a feature that is already mandatory.
6. **Collections say what they are.** A set built from a list, a tuple or a
   generator is a set literal or a set comprehension (`{a, b}`,
   `{x for x in xs}`, not `set([a, b])`, `set((a,))` or `set(x for x in xs)`);
   a dict built from pairs is a dict comprehension (`{k: v for …}`, not
   `dict((k, v) for …)` or `dict([(k, v) …])`); a `defaultdict` names its
   factory (`defaultdict(list)`, not `defaultdict(lambda: [])`); names
   unpacked from a comprehension unpack a generator expression, not a list
   built only to be unpacked; and no type is named twice in an `isinstance`
   or `issubclass` tuple.
7. **Annotations use 3.14's own spellings.** Built-in and `collections.abc`
   generics (`list[int]`, `Mapping[str, int]`), never the `typing` aliases
   (`List`, `Dict`, `typing.Mapping`, `typing.Text`, …); `X | Y` and
   `X | None`, never `Union` or `Optional`; a `type X = …` statement rather
   than `X: TypeAlias = …`; type parameters in brackets (`class Box[T]:`,
   `def first[T](xs: list[T]) -> T:`) rather than a module-level `TypeVar`
   with `Generic[T]`, and not named with a leading underscore; no
   `Generator[T, None, None]` where `Generator[T]` says it; `Self` for a
   `__new__`, `__enter__`, `__aenter__` or an in-place operator returning its
   own instance's type, and for the `__iter__` of a class derived from
   `Iterator`, rather than the class's name, `Iterator` or a `TypeVar` bound
   to it for the purpose; an annotated `__exit__` takes
   `type[BaseException] | None`, `BaseException | None` and
   `TracebackType | None` (or `object`), an annotated `__eq__`/`__ne__` takes
   `object`, and an `__iter__` that is a plain generator returns
   `Iterator[T]` rather than `Generator[…]`; `*args` typed with
   `Unpack[...]` written as `*`; no duplicate member in a union or
   a `Literal`, `Literal[1] | Literal[2]` written `Literal[1, 2]`,
   `type[A] | type[B]` written `type[A | B]`, no `int | float` (`float`
   already admits an `int`), no `Literal` in a union beside its own built-in
   type (`Literal['a'] | str`), no `Literal[None]`, no `Final[Literal[…]]`,
   no `typing.ByteString`, and `Never` rather than `NoReturn` for a
   parameter; `__all__` grows with `+=`, never `.append`, `.extend` or
   `.remove`; a type alias is named in `CamelCase` without a `T`
   suffix; `Generic`, wherever it is still a base, is the last one;
   positional-only parameters are marked with `/`, not with a
   double-underscore name; private type variables, protocols, aliases and
   `TypedDict`s that nothing uses are deleted; and `collections.abc.Set` is
   imported as `AbstractSet`.
8. **No bug-prone pattern.** No mutable default argument — a list, dict or
   set literal, or any call — since it is evaluated once and shared: use
   `None` (or a module-level constant for a call) with the same result for
   every caller. A `raise` of a new exception inside an `except` block says
   how it chains, with `from`; the exception that leaves keeps exactly the
   `__cause__` it has today, so where it had none it still has none
   (`from None`). No expression statement that is only a name, an attribute
   access, a literal other than a string, or a comparison (to read a
   property for its side effect, assign it to `_`); no loop variable
   that is never used and not named `_`-something, and none that rebinds the
   iterable being looped over; no closure inside a loop that reads the loop
   variable; `warnings.warn` passes `stacklevel=` explicitly (1, the
   default, is what leaves a warning attributed where it is today); `zip()`,
   `map()` with several iterables and `itertools.batched()` say `strict=`;
   `getattr` with a constant name and no default, and `setattr` with a
   constant name, are attribute access;
   `callable(x)` rather than `hasattr(x, '__call__')`; `assertRaises` and
   `pytest.raises` never expect a bare `Exception`; no `assert False` and no
   `assert` on a tuple; in `except` clauses, no empty tuple, no one-element
   tuple, no exception named twice in one handler or in two handlers of one
   `try`, and nothing that is not an exception class; no comparison or
   non-string literal used as a statement, no `raise` of a literal, no
   `++x`, no `*args` unpacked after a keyword argument, no `obj.attr: T`
   annotation without a value, no `contextlib.suppress()` with nothing to
   suppress, no `functools.cache` or `functools.lru_cache` on a method, no
   `.strip()`, `.lstrip()` or `.rstrip()` with a string that repeats a
   character
   (`'.txt'` strips a set of characters, not a suffix), no rebinding of
   `os.environ` itself, no f-string as a docstring,
   no abstract base class without an abstract method and no empty method on
   one without `@abstractmethod`, no duplicate value in a set literal, no
   constant key in a dict comprehension, no re-use of an exhausted
   `itertools.groupby` group, no positional `count`/`maxsplit`/`flags` to
   `re.sub`/`re.subn`/`re.split`, and no mutable default for a
   `ContextVar`. Inside an `async def`, nothing blocks (no `time.sleep`,
   `open`, `subprocess` or synchronous HTTP call), nothing busy-waits, and no
   coroutine function takes its own `timeout` parameter.
9. **Nothing is silenced.** No `# noqa` comment appears anywhere in the
   tree, in any spelling, on a line or for a whole file (`# flake8: noqa`),
   and nothing you add — another kind of suppression comment, or a
   configuration file — changes what any of the above means: a pattern a
   comment or a setting hides is still there.

All of it is behaviour-preserving work: every clause in "Everything else
about the tree's observable behaviour" below holds for these files exactly
as it does for the rest.

### Deliberately *not* part of this milestone

* **Rewriting the annotations.** The tree's type surface is
  correct and stays as it is, name for name. This step changes how it is
  stored and what is imported for it, and — where item 7 of the section above
  finds an older spelling — how a type is spelled, never what it says. It
  still has to pass the same strict static type check when you are done, and
  the checker reads the same declarations it read before.
* **Making `TYPE_CHECKING` blocks disappear from the tree by deleting what is
  in them.** Deleting an import that an annotation uses satisfies nothing: the
  block is gone and so is the name the annotation was built from, which is
  strictly worse than where the step started. The imports move; they do not
  go away. This applies to the plugin's blocks exactly as it does to the
  client core's.
* **Adding a dependency.** The standard library is what is available. Nothing
  outside it may be imported for this, and `typing_extensions` in particular
  is not installed.
* **Upgrading the vendored libraries.** Port them in place. Do not pull a
  newer urllib3, requests, elasticsearch-py, dateutil or analytics from an
  index, do not re-vendor one from upstream, and do not replace a vendored
  package with a dependency. Apart from the files this step and the previous
  ones remove, every file the tree has at the start is still there at the end.
* **Auditing the rest of the tree for other things 3.14 changed.** The four
  sections above are the whole of this step.

**Everything else about the tree's observable behaviour stays exactly as it
is.** Return values and their types, dict ordering, the type and the exact
message text of every exception raised, what is written to `stdout`, `stderr`
and the logging handlers, the warnings issued, the HTTP requests the client
puts on the wire and what it makes of the responses that come back.

**Where CPython 3.14 itself answers differently, its answer is the right
one.** A changed error message, a different `repr`, a new field in a standard
library object: where a difference like that shows through it is correct and
expected. Do not add a shim or a branch to reproduce what 3.13 said.

The previous steps' requirements still hold: nothing may reintroduce `six`, a
`__future__` import of any kind, a `sys.version_info` branch, a Python-2
builtin or iteration spelling, a `collections` ABC alias, `fractions.gcd`, a
deleted compatibility module, `imp`, `distutils`, `asynchat`, `asyncore`,
`smtpd` or any of the twenty modules 3.13 retired; no `ssl.wrap_socket`,
`match_hostname`, `CertificateError` or `PROTOCOL_SSLv*` by any spelling, and
the vendored `ssl_match_hostname` backport stays deleted; no
`datetime.utcnow()` or `utcfromtimestamp()`; the three retry sites still
report every attempt as an exception group; the six deprecated definitions
still carry `@warnings.deprecated` and nothing inside the tree still calls
them; the five pieces of shared state that are atomic on the free-threaded
build stay atomic — `RoundRobinSelector.rr`, `ConnectionPool.dead_count`
together with the live list it is paired with, `HTTPDigestAuth.nonce_count`,
and `num_connections` and `num_requests` on both copies of urllib3's
`HTTPConnectionPool`; the type surface stays complete and true, as restated
below; every module still compiles with no `SyntaxWarning`
and no `DeprecationWarning` from the compiler; and, as before, a branch parked
in a comment or in a triple-quoted string has not been removed, and a `u'…'`
prefix must not appear.

Note that `annotations`, the one `__future__` import this tree has been
allowed to carry, is withdrawn by this step. No `__future__` import of any
name survives it.

**The type surface, restated.** The *client core* is
`lib/elasticsearch_connections.py`, `lib/elasticsearch/*.py`,
`lib/elasticsearch/client/utils.py`, `lib/elasticsearch/connection/*.py` and
`lib/elasticsearch/helpers/*.py` (one directory level each, so the other
`client/` API tables are outside it). Together with the plugin's own
`ElasticsearchClient.py`, `events.py`, `commands/*.py` and `panel/*.py` it is
the repository's own surface. Across that surface every public function and
method the tree has (module or class scope, name not starting with `_`, plus
`__init__`, `__enter__`, `__exit__`, `__iter__`, `__next__` and `__call__`)
is still there and fully annotated — every parameter but a leading
`self`/`cls`, and the return of everything but `__init__`; no annotation is or
contains `typing.Any`, and so does no `typing.cast(...)` target or type
variable bound; no `# type: ignore`, `# noqa`, `# mypy:` or
`# pragma: no cover` comment appears and nothing is marked
`typing.no_type_check`; `typing.TYPE_CHECKING` (or a constant standing in for
it) appears only as the whole condition of an `if` with no `else` or `elif`
whose block only declares — imports, class and function definitions,
assignments — since a checker never reads the branch that constant rules out
nor what follows a block of it that leaves; a parameter's annotation names the type
of a default the source spells out; and a return annotation is true of the
body it sits on — `-> None`
over a body that returns a value, a body with `yield` annotated as anything
but an iterator, a concrete non-optional type over a body that never returns
a value (an annotation that mentions `None` or is `object`, a body whose only
exit is `raise`, and a stub body are exempt from that one), and a type that
cannot be true of a value a `return` spells out (a
literal, a list/dict/set/tuple display or comprehension, an f-string, a
comparison or `not`) are all wrong, and `-> object` over such a value counts
as wrong too: it is true of everything and so says nothing about it. A
`return` of a call or a name is not read this way, so where an item above turns a returned
call into such a value, the annotation over it names that value's type —
the one change to what an annotation says that this step can require. The
client core passes a strict static type check run from
`lib/` as the import root, with the vendored packages and the other `client/`
API tables neither checked nor followed into.

There is no build step and nothing to start: the tree is imported as a library
under `python3.14`, with `/app/lib` on `sys.path` the way `ElasticsearchClient.py`
puts it there, and each library the plugin loads from it — `dateutil`,
`requests`, `urllib3`, `analytics`, `elasticsearch` and
`elasticsearch_connections` — imports without raising. A tree whose
libraries do not import under `python3.14` has not reached this milestone.

## 2. Read these local references

**Normative semantics:** the tree in `/app` itself. There is no separate
specification to consult and none is needed — the behaviour this migration
must preserve is defined entirely by the code already in the workspace, and
the target is the public Python language, whose annotation semantics are
specified by PEP 649 and PEP 749, whose `finally` rule is PEP 765, and all
three of which are described in "What's New In Python 3.14".

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
