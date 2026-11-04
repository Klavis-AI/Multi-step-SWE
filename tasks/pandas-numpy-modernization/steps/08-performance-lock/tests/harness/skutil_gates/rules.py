"""The rule catalogue: one entry per construct the ladder retires.

Each rule carries the step that first requires it to be gone. A step's gate set
is every rule with ``since <= step``: once the ladder has retired a construct it
never gets it back, and a step-6 solution that reintroduces ``as_matrix`` fails
step 6 even though ``as_matrix`` was step 2's business.

Two properties every rule here is built to have, because rule 11 of the
authoring prompt asks for them by name:

* **it reads structure, not prose.** ``np.float`` is matched by resolving the
  expression against the file's own import table, so ``import numpy as onp``
  followed by ``onp.float`` is caught and a docstring saying "np.float" is not.
* **it counts expressions, not blocks.** One finding per matching AST node, so
  a function with six ``inplace=True`` keywords contributes six.

A rule with ``library_only`` set is only applied to files inside the installed
package -- gating ``plt.show()`` inside a notebook would be gating the wrong
thing.
"""
from __future__ import print_function, absolute_import, division

import ast

from .astutils import (const_value, dotted, subscript_index, call_keyword,
                       ImportTable, version_rooted)

RULES = []


class Rule(object):
    def __init__(self, name, since, title, why, node_types, match,
                 library_only=False, functions=None):
        self.name = name
        self.since = since
        self.title = title
        self.why = why
        self.node_types = node_types
        self.match = match
        self.library_only = library_only
        # When set, only findings inside one of these functions count. The
        # match is on the dotted owner name ending with the entry, so
        # ``SelectiveScaler.transform`` matches ``transform``.
        self.functions = functions

    def applies_to(self, ctx):
        if self.library_only and ctx.kind != "library":
            return False
        return True

    def owner_ok(self, ctx, node):
        if not self.functions:
            return True
        owner = ctx.owner.get(node, "") or ""
        # A hot path owns a loop that sits directly in it OR in any function
        # nested inside it: extracting the row loop into a local helper
        # (``def _scan(...)`` inside ``filter_collinearity``) must not launder
        # it out of the gate. Match the qualified owner against each hot path
        # and against every function nested beneath one (an ``hp.`` prefix).
        # The trailing dot stops ``filter_collinearity_helper`` from matching
        # the bare ``filter_collinearity``.
        for hp in self.functions:
            if owner == hp or owner.startswith(hp + "."):
                return True
        return False


def rule(name, since, title, why, node_types, library_only=False,
         functions=None):
    def wrap(fn):
        RULES.append(Rule(name, since, title, why, node_types, fn,
                          library_only=library_only, functions=functions))
        return fn
    return wrap


def by_step(step):
    """Every rule in force at step *step*, in catalogue order."""
    return [r for r in RULES if r.since <= step]


def by_name(name):
    for r in RULES:
        if r.name == name:
            return r
    raise KeyError(name)


# --------------------------------------------------------------------------
# helpers shared by several rules


def _q(ctx, node):
    """The fully-qualified name an expression refers to, or None."""
    return ctx.table.resolve(dotted(node))


def _dyn_module(ctx, node):
    """The module an expression denotes, dotted or fetched at runtime."""
    if isinstance(node, ast.Call):
        if _q(ctx, node.func) in ("__import__", "importlib.import_module"):
            if node.args:
                name = const_value(node.args[0], None)
                return name if isinstance(name, str) else None
        return None
    return _q(ctx, node)


def _dyn_attr(ctx, node):
    """``getattr(<module>, "name")`` read as the qualified name it fetches.

    ``getattr(np.random, "seed")`` is the same access as ``np.random.seed``,
    and ``getattr(__import__("numpy.random", fromlist=["choice"]), "choice")``
    is the same access as ``from numpy.random import choice``. A gate that only
    reads dotted names is answered by spelling the dot differently, which is
    not a migration. A name that is not a literal comes back as ``<module>.*``
    -- which attribute is being taken cannot be read here, but which module is
    being reached into can, and that is the part these rules are about.
    """
    if not isinstance(node, ast.Call) or _q(ctx, node.func) != "getattr":
        return None
    if len(node.args) < 2:
        return None
    module = _dyn_module(ctx, node.args[0])
    if module is None:
        return None
    name = const_value(node.args[1], None)
    return module + "." + (name if isinstance(name, str) else "*")


def _rng_target(ctx, node):
    """``(qualified, built_at_runtime)`` for the callee of a Call.

    The Call is tried as a dynamic fetch first, because ``_q`` returns an
    unresolved root verbatim and would otherwise answer ``"getattr"`` and stop.
    The fetch is the site, so ``getattr(np.random, "rand")(500)`` counts once,
    on the inner ``getattr`` -- the outer call's callee is a Call, which
    neither branch resolves.
    """
    q = _dyn_attr(ctx, node)
    if q is not None:
        return q, True
    return _q(ctx, node.func), False


def _said(qualified, built_at_runtime):
    if built_at_runtime:
        return qualified + " (name built at runtime)"
    return qualified


def _under(qualified, *prefixes):
    if qualified is None:
        return False
    for p in prefixes:
        if qualified == p or qualified.startswith(p + "."):
            return True
    return False


def _imported_names(node):
    """(bound-or-dotted module path, alias node) for one import statement."""
    if isinstance(node, ast.Import):
        for alias in node.names:
            yield alias.name, alias
    elif isinstance(node, ast.ImportFrom):
        if node.level:
            return
        module = node.module or ""
        for alias in node.names:
            yield module + ("." + alias.name if alias.name != "*" else ""), alias


# --------------------------------------------------------------------------
# step 1 -- reproducibility, and the warnings the tree raises about itself

_GLOBAL_DRAWS = frozenset("""
rand randn randint random_integers random_sample random ranf sample
bytes choice permutation shuffle beta binomial chisquare dirichlet
exponential f gamma geometric gumbel hypergeometric laplace logistic
lognormal logseries multinomial multivariate_normal negative_binomial
noncentral_chisquare noncentral_f normal pareto poisson power rayleigh
standard_cauchy standard_exponential standard_gamma standard_normal
standard_t triangular uniform vonmises wald weibull zipf get_state
set_state
""".split())


@rule("global-rng-seed", 1,
      "seeds the process-global NumPy RNG",
      "A library that calls np.random.seed decides the seed for every other "
      "library in the process. Reproducibility has to come from an explicitly "
      "owned generator instead.",
      (ast.Call,))
def _global_rng_seed(node, ctx):
    q, dyn = _rng_target(ctx, node)
    if q in ("numpy.random.seed", "numpy.random.mtrand.seed"):
        return _said(q, dyn)
    return None


@rule("global-rng-draw", 1,
      "draws from the process-global NumPy RNG",
      "Draws from numpy.random.* consume the global bit stream, so the result "
      "depends on what every other caller in the process drew first.",
      (ast.Call,))
def _global_rng_draw(node, ctx):
    q, dyn = _rng_target(ctx, node)
    if q is None or not q.startswith("numpy.random."):
        return None
    tail = q.split(".")[-1]
    # `*` is a runtime-built attribute name on the global RNG module. Which
    # attribute it is cannot be read statically; that it is a reach into
    # numpy.random can be, and there is no spelling of a legitimate fix that
    # needs one. Leaving it uncounted would make the unreadable spelling the
    # cheapest one in the tree.
    if tail == "*" or tail in _GLOBAL_DRAWS:
        return _said(q, dyn)
    return None


_STDLIB_DRAWS = frozenset(["seed", "random", "randint", "randrange", "choice",
                           "choices", "shuffle", "sample", "uniform", "gauss",
                           "normalvariate"])


@rule("stdlib-global-rng", 1,
      "draws from the stdlib global RNG",
      "random.shuffle and friends share one module-level Mersenne Twister; "
      "the same argument holds as for numpy.random.",
      (ast.Call,))
def _stdlib_rng(node, ctx):
    q, dyn = _rng_target(ctx, node)
    if q is None or not q.startswith("random."):
        return None
    tail = q.split(".")[-1]
    if tail == "*" or tail in _STDLIB_DRAWS:
        return _said(q, dyn)
    return None


# The modules whose module-level functions *are* the global streams. Reaching
# into one of these through a name assembled at runtime is the subject of the
# rule below.
_GLOBAL_RNG_MODULES = ("numpy.random", "numpy.random.mtrand", "random")


def _retired_in(module):
    """The names of *module* this step has retired, or None if it is not one.

    Not everything these modules hold: ``RandomState`` is the replacement the
    instruction itself names, and failing a tree for fetching it oddly would
    cost an honest submission the whole carried family.
    """
    if module not in _GLOBAL_RNG_MODULES:
        return None
    if module == "random":
        return _STDLIB_DRAWS
    return _GLOBAL_DRAWS | frozenset(["seed"])


def _retired_key(module, name):
    """Is *name* a retired member of *module*, or unreadable at all?

    ``None`` means the key was not a literal -- which attribute is being taken
    cannot be read here. No honest fix needs a computed attribute name on a
    global RNG module, and leaving the unreadable case out would make it the
    cheapest spelling in the tree, so it counts.
    """
    retired = _retired_in(module)
    if retired is None:
        return False
    return name is None or name in retired


@rule("dynamic-legacy-access", 1,
      "reaches a global RNG module through a name built at runtime",
      "getattr(numpy.random, 'seed'), sys.modules['numpy.random'].seed, "
      "vars(numpy.random)['seed'], operator.attrgetter('seed')(numpy.random) "
      "and eval('numpy.random.seed') all name the same function as "
      "numpy.random.seed while defeating every gate that reads the access as "
      "written -- ruff sees nothing at all, and an AST rule only sees it if it "
      "knows to look through the fetch. The tree the step is handed contains "
      "none of these, and no honest spelling of the fix needs one: the "
      "replacement for a global draw is a RandomState the caller owns, which "
      "is named directly. So this is carried, not new -- introducing an "
      "indirection is a regression, whatever it is pointed at. What cannot be "
      "reached this way is a module name assembled from non-constant parts; "
      "that is undecidable here, and the runtime reproducibility probes are "
      "what stand behind it.",
      (ast.Call, ast.Subscript))
def _dynamic_legacy_access(node, ctx):
    if isinstance(node, ast.Subscript):
        return _dynamic_subscript(node, ctx)

    fetched = _dyn_attr(ctx, node)
    if fetched is not None:
        module, _, tail = fetched.rpartition(".")
        if _retired_key(module, None if tail == "*" else tail):
            return fetched + " (name built at runtime)"
        return None

    called = _q(ctx, node.func)
    if called in ("__import__", "importlib.import_module"):
        name = const_value(node.args[0], None) if node.args else None
        if isinstance(name, str) and name in _GLOBAL_RNG_MODULES:
            return name + " (imported at runtime)"

    # operator.attrgetter("seed")(numpy.random) -- the fetch is the inner call
    # and the module is the outer call's argument, so it can only be read from
    # out here. methodcaller has the same shape and the same effect.
    if isinstance(node.func, ast.Call) and node.args:
        inner = _q(ctx, node.func.func)
        if inner in ("operator.attrgetter", "operator.methodcaller"):
            module = _dyn_module(ctx, node.args[0])
            name = const_value(node.func.args[0], None) if node.func.args else None
            if module is not None and _retired_key(
                    module, name if isinstance(name, str) else None):
                return "%s.%s (%s)" % (module, name or "*", inner)

    # eval("numpy.random.seed")(0) and exec of the same. Only a constant string
    # is readable; one assembled at runtime is not, which the docstring says.
    if called in ("eval", "exec") and node.args:
        text = const_value(node.args[0], None)
        if isinstance(text, str):
            hit = _rng_name_in_text(text, ctx)
            if hit is not None:
                return "%s (in a string passed to %s)" % (hit, called)
    return None


def _rng_name_in_text(text, ctx):
    """A retired global-RNG access spelled inside a string, or None.

    The spellings searched for are the canonical ones *and* whatever this file
    binds them to: a module that does ``import numpy as np`` can write
    ``eval("np.random.seed")``, and matching only "numpy.random.seed" would
    hand that spelling back for free. Resolution is per file, exactly as for
    an access written out, so the two paths agree about what a name means.
    """
    prefixes = {}
    for module in _GLOBAL_RNG_MODULES:
        prefixes[module] = module
    for bound, target in (ctx.table.aliases or {}).items():
        for module in _GLOBAL_RNG_MODULES:
            if target == module:
                prefixes[bound] = module
            elif module.startswith(target + "."):
                # ``import numpy as np`` binds np -> numpy, so np.random.seed
                # is numpy.random.seed with the head swapped.
                prefixes[bound + module[len(target):]] = module
    for spelling in sorted(prefixes, key=len, reverse=True):
        module = prefixes[spelling]
        for name in sorted(_retired_in(module)):
            if spelling + "." + name in text:
                return spelling + "." + name
    return None


def _dynamic_subscript(node, ctx):
    """``sys.modules[...]``, ``vars(m)[...]`` and ``m.__dict__[...]``."""
    # subscript_index, not node.slice: the scanner runs on 3.7 inside the
    # step-1 image, where the key is wrapped in ast.Index. Reading it raw makes
    # every key unreadable there -- which silently drops the sys.modules arm
    # and turns `numpy.random.__dict__["RandomState"]`, an honest fetch of the
    # replacement, into a finding that would zero the whole carried family.
    key = const_value(subscript_index(node), None)
    if not isinstance(key, str):
        key = None

    holder = node.value
    # sys.modules["numpy.random"] -- the module is the *key*, not the object.
    if _q(ctx, holder) == "sys.modules":
        if key in _GLOBAL_RNG_MODULES:
            return key + " (fetched from sys.modules)"
        return None

    # numpy.random.__dict__["seed"] and vars(numpy.random)["seed"].
    module = None
    if isinstance(holder, ast.Attribute) and holder.attr == "__dict__":
        module = _dyn_module(ctx, holder.value)
        how = "__dict__"
    elif (isinstance(holder, ast.Call) and _q(ctx, holder.func) == "vars"
          and holder.args):
        module = _dyn_module(ctx, holder.args[0])
        how = "vars()"
    if module is not None and _retired_key(module, key):
        return "%s.%s (fetched through %s)" % (module, key or "*", how)
    return None


_COLLECTIONS_ABC = frozenset("""
Awaitable Coroutine AsyncIterable AsyncIterator AsyncGenerator Hashable
Iterable Iterator Generator Reversible Sized Container Callable Collection
Set MutableSet Mapping MutableMapping MappingView KeysView ItemsView
ValuesView Sequence MutableSequence ByteString
""".split())


@rule("collections-abc-alias", 1,
      "imports an ABC from collections rather than collections.abc",
      "The aliases were deprecated in 3.3 and removed in 3.10; under -W error "
      "they are already a DeprecationWarning on the step-1 interpreter.",
      (ast.Import, ast.ImportFrom, ast.Attribute))
def _collections_abc(node, ctx):
    if isinstance(node, ast.Attribute):
        q = _q(ctx, node)
        if q and q.startswith("collections.") and \
                q.split(".")[-1] in _COLLECTIONS_ABC and \
                not q.startswith("collections.abc."):
            return q
        return None
    for path, _alias in _imported_names(node):
        head, _, tail = path.rpartition(".")
        if head == "collections" and tail in _COLLECTIONS_ABC:
            return path
    return None


# --------------------------------------------------------------------------
# step 2 -- pandas 1.x, NumPy 1.2x, scikit-learn 0.24

_PANDAS_REMOVED_METHODS = {
    "as_matrix": "removed in pandas 1.0; use .to_numpy()",
    "get_values": "removed in pandas 1.0",
    "get_value": "removed in pandas 1.0",
    "set_value": "removed in pandas 1.0",
    "convert_objects": "removed in pandas 0.25",
    "ix": "removed in pandas 1.0",
}


@rule("pandas-removed-method", 2,
      "calls a pandas accessor removed in pandas 1.0",
      "as_matrix/get_values/ix and the rest of the 1.0 removals. Matched on "
      "the attribute itself because the receiver is a runtime value, and no "
      "other library in this tree defines any of these names.",
      (ast.Attribute,))
def _pandas_removed(node, ctx):
    if node.attr in _PANDAS_REMOVED_METHODS:
        return node.attr
    return None


@rule("pandas-inplace-true", 2,
      "passes inplace=True",
      "In-place mutation is what Copy-on-Write ends; every site has to become "
      "an assignment before pandas 2 can be turned on.",
      (ast.Call,))
def _inplace_true(node, ctx):
    kw = call_keyword(node, "inplace")
    if kw is None:
        return None
    val = const_value(kw.value, None)
    if val is True or val == 1:
        return "inplace=True"
    return None


# np.long is here for a reason that is easy to get backwards: NumPy 1.24
# removed it as a Python-2 alias, and NumPy 2.0 then *reintroduced* the name
# with a different meaning (the C long). A spelling that means two things
# across the ladder is exactly the one to retire, so it stays gated from step 2
# onwards even though step 5's NumPy would accept it again.
_NUMPY_REMOVED_ALIASES = frozenset(
    "bool int float complex object str long unicode".split())


@rule("numpy-removed-alias", 2,
      "uses a NumPy builtin alias",
      "np.float and friends were deprecated in NumPy 1.20 and removed in "
      "1.24. Resolved through the import table, so an alias other than `np` "
      "is caught and a docstring mentioning np.float is not.",
      (ast.Attribute, ast.Name, ast.Import, ast.ImportFrom))
def _numpy_alias(node, ctx):
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        for path, _alias in _imported_names(node):
            head, _, tail = path.rpartition(".")
            if head == "numpy" and tail in _NUMPY_REMOVED_ALIASES:
                return path
        return None
    if isinstance(node, ast.Name):
        q = ctx.table.aliases.get(node.id)
        if q is None:
            return None
    else:
        q = _q(ctx, node)
    if q is None:
        return None
    head, _, tail = q.rpartition(".")
    if head == "numpy" and tail in _NUMPY_REMOVED_ALIASES:
        return q
    return None


# The step-2 counterpart of _GLOBAL_RNG_MODULES: module -> the names this step
# retires on it. `None` means the whole module is gone, so any name fetched out
# of it counts.
_RETIRED_AT_2 = {
    "numpy": _NUMPY_REMOVED_ALIASES,
    "sklearn.externals": None,
    "sklearn.grid_search": None,
    "sklearn.cross_validation": None,
    "sklearn.learning_curve": None,
    "sklearn.metrics.scorer": None,
    "sklearn.utils.testing": None,
    "seaborn": frozenset(["plt"]),
}


def _retired_at_2(module, name):
    """Is *name* a step-2 retired member of *module*, or unreadable at all?

    Same rule as ``_retired_key``'s: ``None`` for the name means the attribute
    could not be read here, and it counts. Leaving the unreadable case out
    would make ``getattr(np, _ALIAS)`` -- the exact spelling the fixture used
    -- the cheapest way through the gate.
    """
    if module not in _RETIRED_AT_2:
        return False
    retired = _RETIRED_AT_2[module]
    return retired is None or name is None or name in retired


@rule("dynamic-retired-name", 2,
      "reaches a name this step retires through a name built at runtime",
      "getattr(np, 'flo' + 'at'), vars(numpy)['float'], "
      "numpy.__dict__['int'], sys.modules['sklearn.externals'] and "
      "importlib.import_module('sklearn.grid_search') all reach exactly what "
      "properties 2, 7 and 9 retire, while every gate that reads the access "
      "as written sees nothing -- ruff's NPY001 included. Step 1 carries the "
      "same guard for the global RNG (dynamic-legacy-access); this is that "
      "guard pointed at the names step 2 retires, and it exists because a "
      "synthetic fixture cleared every construct row this way and scored "
      "1.000000 (evidence/item6/step02-fixtures.txt, hide-behind-getattr). "
      "Carried rather than new: the handed-over tree contains none of these, "
      "and no honest spelling of the migration needs one -- the replacement "
      "for np.float is the builtin float, named directly. What is still out "
      "of reach is a module assembled from non-constant parts; that is "
      "undecidable here, and the behaviour corpus is what stands behind it.",
      (ast.Call, ast.Subscript))
def _dynamic_retired_name(node, ctx):
    if isinstance(node, ast.Subscript):
        key = const_value(subscript_index(node), None)
        if not isinstance(key, str):
            key = None
        holder = node.value
        if _q(ctx, holder) == "sys.modules":
            # Only the modules that are *gone*. `numpy` is in _RETIRED_AT_2
            # for its retired members, not because the module went away, and
            # sys.modules["numpy"] is an honest fetch of a live module.
            return (key + " (fetched from sys.modules)"
                    if _RETIRED_AT_2.get(key, False) is None else None)
        module = None
        if isinstance(holder, ast.Attribute) and holder.attr == "__dict__":
            module, how = _dyn_module(ctx, holder.value), "__dict__"
        elif (isinstance(holder, ast.Call) and _q(ctx, holder.func) == "vars"
              and holder.args):
            module, how = _dyn_module(ctx, holder.args[0]), "vars()"
        if module is not None and _retired_at_2(module, key):
            return "%s.%s (fetched through %s)" % (module, key or "*", how)
        return None

    fetched = _dyn_attr(ctx, node)
    if fetched is not None:
        module, _, tail = fetched.rpartition(".")
        if _retired_at_2(module, None if tail == "*" else tail):
            return fetched + " (name built at runtime)"
        return None

    called = _q(ctx, node.func)
    if called in ("__import__", "importlib.import_module") and node.args:
        name = const_value(node.args[0], None)
        # As above: importing numpy at runtime is not a finding; importing a
        # module this scikit-learn no longer ships is.
        if isinstance(name, str) and _RETIRED_AT_2.get(name, False) is None:
            return name + " (imported at runtime)"

    # operator.attrgetter("float")(numpy) -- the fetch is the inner call and
    # the module is the outer call's argument, exactly as at step 1.
    if isinstance(node.func, ast.Call) and node.args:
        inner = _q(ctx, node.func.func)
        if inner in ("operator.attrgetter", "operator.methodcaller"):
            module = _dyn_module(ctx, node.args[0])
            name = const_value(node.func.args[0], None) if node.func.args else None
            if module is not None and _retired_at_2(
                    module, name if isinstance(name, str) else None):
                return "%s.%s (%s)" % (module, name or "*", inner)

    if called in ("eval", "exec") and node.args:
        text = const_value(node.args[0], None)
        if isinstance(text, str):
            hit = _retired2_name_in_text(text, ctx)
            if hit is not None:
                return "%s (in a string passed to %s)" % (hit, called)
    return None


def _retired2_name_in_text(text, ctx):
    """A step-2 retired name spelled inside a string, or None.

    The same resolution `_rng_name_in_text` does, over `_RETIRED_AT_2`: a file
    that does ``import numpy as np`` can write ``eval("np.float")``, and
    matching only the canonical spelling would hand that one back for free.
    """
    prefixes = {}
    for module in _RETIRED_AT_2:
        prefixes[module] = module
    for bound, target in (ctx.table.aliases or {}).items():
        for module in _RETIRED_AT_2:
            if target == module:
                prefixes[bound] = module
            elif module.startswith(target + "."):
                prefixes[bound + module[len(target):]] = module
    for spelling in sorted(prefixes, key=len, reverse=True):
        names = _RETIRED_AT_2[prefixes[spelling]]
        if names is None:
            if spelling in text:
                return spelling
            continue
        for name in sorted(names):
            if spelling + "." + name in text:
                return spelling + "." + name
    return None


@rule("sklearn-externals", 2,
      "imports through sklearn.externals",
      "sklearn.externals.six and .joblib were removed in scikit-learn 0.23; "
      "six and joblib are their own packages.",
      (ast.Import, ast.ImportFrom, ast.Attribute))
def _sklearn_externals(node, ctx):
    if isinstance(node, ast.Attribute):
        q = _q(ctx, node)
        return q if _under(q, "sklearn.externals") else None
    for path, _alias in _imported_names(node):
        if _under(path, "sklearn.externals"):
            return path
    return None


@rule("sklearn-moved-module", 2,
      "imports a scikit-learn module that has moved or gone",
      "sklearn.metrics.scorer, sklearn.grid_search, sklearn.cross_validation "
      "and sklearn.learning_curve were all removed between 0.20 and 0.24.",
      (ast.Import, ast.ImportFrom, ast.Attribute))
def _sklearn_moved(node, ctx):
    gone = ("sklearn.metrics.scorer", "sklearn.grid_search",
            "sklearn.cross_validation", "sklearn.learning_curve",
            "sklearn.datasets.samples_generator",
            "sklearn.metrics.classification", "sklearn.metrics.regression",
            "sklearn.utils.testing")
    if isinstance(node, ast.Attribute):
        q = _q(ctx, node)
        return q if _under(q, *gone) else None
    for path, _alias in _imported_names(node):
        if _under(path, *gone):
            return path
    return None


@rule("py2-future-import", 2,
      "carries a __future__ import",
      "Python-2 compatibility scaffolding. Nothing in the ladder runs on a "
      "Python where any of these is not already the default.",
      (ast.ImportFrom,))
def _future_import(node, ctx):
    if node.module == "__future__":
        return "__future__." + ",".join(a.name for a in node.names)
    return None


_PY2_BUILTINS = frozenset("long unicode basestring xrange raw_input "
                          "cmp reduce unichr".split())
_PY2_METHODS = frozenset("iteritems itervalues iterkeys has_key".split())


@rule("py2-builtin", 2,
      "names a Python-2 builtin or dict method",
      "long/xrange/basestring and the iter* dict methods do not exist on any "
      "interpreter in this ladder; a reference to one is dead code guarded by "
      "a version branch.",
      (ast.Name, ast.Attribute))
def _py2_builtin(node, ctx):
    if isinstance(node, ast.Name):
        if node.id in _PY2_BUILTINS and node.id not in ctx.table.aliases:
            return node.id
        return None
    if node.attr in _PY2_METHODS:
        return node.attr
    return None


@rule("py2-version-branch", 2,
      "branches on the Python major version",
      "sys.version_info[0] == 2 branches are unreachable on every interpreter "
      "the ladder uses, and they hide which half of the code is live.",
      (ast.Compare, ast.Assign, ast.AnnAssign))
def _version_branch(node, ctx):
    # Three node types because the construct is written two ways in this tree
    # and the second one is invisible to a rule that only reads the first.
    #
    # Direct:   if sys.version_info[0] < 3:              -- setup.py:15
    # Aliased:  VERSION_MAJOR = sys.version_info.major   -- fixes.py:34
    #           if VERSION_MAJOR > 2:                    -- fixes.py:38
    #
    # Six of this repository's nine sites are the aliased form. Counting only
    # the direct one understates the work by two thirds, and -- the reason it
    # is a hole rather than an inaccuracy -- it can be *cleared* by hoisting
    # the subscript into a local, which leaves the dead branch exactly where it
    # was. The binding itself is reported too, so deleting the branches and
    # keeping the local is still one finding.
    if isinstance(node, (ast.Assign, ast.AnnAssign)):
        if node.value is None or not version_rooted(node.value, ctx.table):
            return None
        targets = node.targets if isinstance(node, ast.Assign) else [node.target]
        bound = []
        for target in targets:
            if isinstance(target, ast.Name):
                bound.append(target.id)
            elif isinstance(target, (ast.Tuple, ast.List)):
                bound.extend(e.id for e in target.elts
                             if isinstance(e, ast.Name))
        if not bound:
            return None
        return "binds %s to sys.version_info" % ", ".join(sorted(bound))

    left = node.left
    if isinstance(left, ast.Name) and left.id in ctx.version_names:
        return "%s comparison (bound to sys.version_info)" % left.id
    if isinstance(left, ast.Subscript):
        q = _q(ctx, left.value)
    else:
        q = _q(ctx, left)
    if q not in ("sys.version_info", "sys.version"):
        return None
    for comp in node.comparators:
        val = const_value(comp, None)
        if val in (2, 3) or (isinstance(val, tuple) and val and val[0] in (2, 3)):
            return "sys.version_info comparison"
    return "sys.version_info comparison"


# --------------------------------------------------------------------------
# step 3 -- scikit-learn 1.x

@rule("sklearn-private-api", 3,
      "imports a private scikit-learn module",
      "The repo re-implements GridSearchCV against scikit-learn 0.18 private "
      "internals. Private modules carry no compatibility promise and every "
      "one of these moved before 1.0.",
      (ast.Import, ast.ImportFrom))
def _sklearn_private(node, ctx):
    for path, _alias in _imported_names(node):
        if not path.startswith("sklearn."):
            continue
        for seg in path.split(".")[1:]:
            if seg.startswith("_") and not seg.startswith("__"):
                return path
    return None


@rule("sklearn-load-boston", 3,
      "uses the Boston housing dataset",
      "load_boston was removed in scikit-learn 1.2 over the ethical problem "
      "documented in its own deprecation notice.",
      (ast.Call, ast.Import, ast.ImportFrom, ast.Attribute))
def _load_boston(node, ctx):
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        for path, _alias in _imported_names(node):
            if path.endswith(".load_boston") or path == "load_boston":
                return path
        return None
    target = node.func if isinstance(node, ast.Call) else node
    q = _q(ctx, target)
    if q and q.split(".")[-1] == "load_boston":
        return q
    return None


@rule("sklearn-grid-scores", 3,
      "reads grid_scores_",
      "The 0.17-era search attribute; cv_results_ replaced it in 0.18 and it "
      "was removed in 0.20. Only grid_scores_ is matched: n_folds and n_iter "
      "were also 0.17 spellings, but this package defines attributes with "
      "both names on its own classes, so matching them would flag a dozen "
      "sites that have nothing to do with scikit-learn.",
      (ast.Attribute,))
def _grid_scores(node, ctx):
    if node.attr == "grid_scores_":
        return node.attr
    return None


# --------------------------------------------------------------------------
# step 4 -- pandas 2.x with Copy-on-Write

@rule("pandas-chained-assignment", 4,
      "assigns through a chained indexer",
      "df[a][b] = v writes into a temporary. Under Copy-on-Write the write is "
      "silently lost; pandas 3 raises. Matched as an assignment whose target "
      "is a subscript of a subscript or of a call, which is the shape of the "
      "construct rather than its spelling.",
      (ast.Assign, ast.AugAssign))
def _chained_assign(node, ctx):
    targets = node.targets if isinstance(node, ast.Assign) else [node.target]
    for tgt in targets:
        if not isinstance(tgt, ast.Subscript):
            continue
        inner = tgt.value
        if isinstance(inner, ast.Subscript):
            return "subscript of subscript"
        if isinstance(inner, ast.Call):
            return "subscript of call"
        if isinstance(inner, ast.Attribute) and inner.attr in ("values", "_values"):
            # ``df.values[i] = v`` writes into whatever array .values handed
            # back. Under Copy-on-Write that is a copy and the write is lost.
            # ``df.loc[x] = v`` is deliberately *not* here: it is the correct
            # spelling, and flagging it would penalise the fix.
            return "assignment through ." + inner.attr
    return None


def _is_groupby_callee(func, ctx):
    """True when *func* is the ``groupby`` attribute, however it was fetched.

    ``df.groupby(...)`` reads as an Attribute and ``getattr(df, "groupby")(...)``
    as a Call to the builtin, but they are the same access -- the same argument
    ``_dyn_attr`` makes for the module-level fetches step 2 closed. Scoped to
    the literal name: a ``getattr`` whose attribute is computed is not read as a
    groupby here, because which attribute it takes cannot be known.
    """
    if isinstance(func, ast.Attribute):
        return func.attr == "groupby"
    if isinstance(func, ast.Call) and _q(ctx, func.func) == "getattr":
        return (len(func.args) >= 2
                and const_value(func.args[1], None) == "groupby")
    return False


@rule("pandas-groupby-implicit-observed", 4,
      "calls groupby without observed=",
      "The default flipped to True in pandas 3. A groupby over a categorical "
      "that does not say which it wants changes its row count on upgrade. "
      "Matched on the call whether the method is dotted or fetched by name.",
      (ast.Call,))
def _groupby_observed(node, ctx):
    if not _is_groupby_callee(node.func, ctx):
        return None
    if call_keyword(node, "observed") is None:
        return "groupby(...) without observed="
    return None


def _chained_assignment_option(node):
    """``<anything>.mode.chained_assignment`` as an assignment target.

    ``pd.options.mode.chained_assignment = None`` is the attribute spelling of
    ``set_option("mode.chained_assignment", None)`` and turns off the same
    warning. The two-attribute tail is what identifies it, so whatever alias
    stands in front of ``.mode`` does not matter; nothing else under ``mode``
    is matched.
    """
    return (isinstance(node, ast.Attribute)
            and node.attr == "chained_assignment"
            and isinstance(node.value, ast.Attribute)
            and node.value.attr == "mode")


@rule("pandas-silences-chained-assignment", 4,
      "turns the chained-assignment warning off",
      "Setting mode.chained_assignment to None is the suppression this step "
      "exists to make unnecessary. Counted in either spelling, because the "
      "option call and the attribute assignment are the same switch: "
      "set_option/option_context with that option name, and an assignment "
      "whose target ends in .mode.chained_assignment.",
      (ast.Call, ast.Assign, ast.AnnAssign))
def _silence_chained(node, ctx):
    if isinstance(node, ast.Call):
        q = _q(ctx, node.func)
        if q not in ("pandas.set_option", "pandas.options.mode",
                     "pandas.option_context"):
            return None
        for arg in node.args:
            if const_value(arg, None) == "mode.chained_assignment":
                return "set_option('mode.chained_assignment')"
        return None
    targets = node.targets if isinstance(node, ast.Assign) else [node.target]
    for tgt in targets:
        if _chained_assignment_option(tgt):
            return "assignment to mode.chained_assignment"
    return None


# --------------------------------------------------------------------------
# step 5 -- NumPy 2.0

@rule("numpy-distutils", 5,
      "builds through numpy.distutils",
      "numpy.distutils was removed in NumPy 1.26/2.0. Both extension builds "
      "in this tree go through it, so the build system is the migration.",
      (ast.Import, ast.ImportFrom, ast.Attribute))
def _numpy_distutils(node, ctx):
    if isinstance(node, ast.Attribute):
        q = _q(ctx, node)
        return q if _under(q, "numpy.distutils") else None
    for path, _alias in _imported_names(node):
        if _under(path, "numpy.distutils"):
            return path
    return None


_NUMPY_EXPIRED = frozenset("""
in1d row_stack alltrue sometrue cumproduct product round_ float_ complex_
unicode_ string_ NaN NAN Inf Infinity infty NINF PINF NZERO PZERO mat
issctype set_string_function asfarray deprecate deprecate_with_doc safe_eval
msort compare_chararrays who source lookfor issubsctype maximum_sctype
obj2sctype sctype2char sctypes find_common_type get_array_wrap disp
add_newdoc_ufunc fastCopyAndTranspose recfromcsv recfromtxt trapz
""".split())


@rule("numpy-expired-member", 5,
      "uses a NumPy member the 2.0 expiry removed",
      "The NumPy 2.0 migration guide's expired-attributes table: np.in1d, "
      "np.row_stack, np.alltrue, np.cumproduct, np.trapz and the rest.",
      (ast.Attribute, ast.Name, ast.Import, ast.ImportFrom))
def _numpy_expired(node, ctx):
    if isinstance(node, (ast.Import, ast.ImportFrom)):
        for path, _alias in _imported_names(node):
            head, _, tail = path.rpartition(".")
            if head == "numpy" and tail in _NUMPY_EXPIRED:
                return path
        return None
    if isinstance(node, ast.Name):
        q = ctx.table.aliases.get(node.id)
    else:
        q = _q(ctx, node)
    if q is None:
        return None
    head, _, tail = q.rpartition(".")
    if head == "numpy" and tail in _NUMPY_EXPIRED:
        return q
    return None


@rule("numpy-copy-false", 5,
      "passes copy=False to np.array",
      "In NumPy 2 copy=False means 'never copy' and raises when a copy would "
      "be needed, where it used to mean 'copy only if you must'.",
      (ast.Call,))
def _copy_false(node, ctx):
    q = _q(ctx, node.func)
    if q != "numpy.array":
        return None
    kw = call_keyword(node, "copy")
    if kw is not None and const_value(kw.value, None) is False:
        return "np.array(copy=False)"
    return None


# --------------------------------------------------------------------------
# step 6 -- pandas 3.0 on Python 3.12

@rule("stdlib-distutils", 6,
      "imports distutils",
      "distutils was removed from the standard library in Python 3.12.",
      (ast.Import, ast.ImportFrom))
def _stdlib_distutils(node, ctx):
    for path, _alias in _imported_names(node):
        if _under(path, "distutils"):
            return path
    return None


_PANDAS_EXPIRED_3 = {
    "applymap": "removed in pandas 3; DataFrame.map",
    "swapaxes": "removed in pandas 3",
    "is_monotonic": "removed in pandas 2.0; is_monotonic_increasing",
    "lookup": "removed in pandas 2.0",
    "mad": "removed in pandas 2.0",
    # ``iteritems`` belongs to py2-builtin at step 2, which retires it four
    # steps earlier. Listing it here as well would double-count the same
    # seven expressions and hand step 6 a gate step 2 had already closed.
    "iteritems": None,
    "append": None,     # too ambiguous to gate; kept here as documentation
    "bool": None,
}


@rule("pandas-expired-3", 6,
      "calls a pandas accessor the 3.0 expiry removed",
      "applymap, swapaxes, is_monotonic, lookup and mad. append and bool are "
      "deliberately not gated: list.append and the builtin make an "
      "attribute-name match meaningless for those two.",
      (ast.Attribute,))
def _pandas_expired3(node, ctx):
    if _PANDAS_EXPIRED_3.get(node.attr):
        return node.attr
    return None


@rule("pandas-inplace-kwarg", 6,
      "passes inplace= at all",
      "Step 2 retired inplace=True. pandas 3 deprecates the keyword outright, "
      "so by this step no call may name it, whatever the value.",
      (ast.Call,))
def _inplace_any(node, ctx):
    if call_keyword(node, "inplace") is not None:
        return "inplace="
    return None


@rule("pandas-fillna-downcast", 6,
      "relies on fillna downcasting",
      "The downcast= keyword was removed in pandas 3 and the implicit "
      "downcast it controlled is gone with it.",
      (ast.Call,))
def _fillna_downcast(node, ctx):
    if isinstance(node.func, ast.Attribute) and \
            node.func.attr in ("fillna", "ffill", "bfill", "where", "mask"):
        if call_keyword(node, "downcast") is not None:
            return node.func.attr + "(downcast=)"
    return None


# --------------------------------------------------------------------------
# step 6 -- pandas 3 default string dtype (PDEP-14)
#
# pandas 3.0 turns on ``future.infer_string`` by default, so a column of Python
# strings is no longer the ``object`` dtype -- it is the new ``str`` StringDtype
# (docs: pandas.pydata.org/docs/user_guide/migration-3-strings.html). Code that
# recognises text columns by comparing a dtype to the literal ``'object'``, or
# by ``select_dtypes(include=['object'])``, silently stops seeing them: the
# equality is False against the StringDtype, and select_dtypes keeps matching
# only through a compat path that now raises Pandas4Warning. The migration is to
# admit ``'str'`` wherever ``'object'`` is admitted. This gate is what makes the
# detection work carry weight the behaviour corpus cannot supply on its own: the
# corpus exercises only a handful of these sites, so without the gate a tree
# could skip the whole string-dtype migration and still score high.
#
# It must accept the natural fix and only the natural fix. ``dtype == 'object'``
# paired with a test that admits the ``str`` dtype in the same ``or`` / ``|``
# group is exempt, in every spelling (``a == 'object' or a == 'str'``,
# ``(d == 'object') | (d == 'str')``, and ``x in ('object', 'str')`` which never
# spells ``== 'object'`` at all); ``select_dtypes`` is exempt once ``'str'``
# joins the include list. pandas' own migration guide for the new string dtype
# recommends two spellings that work on pandas 2 and 3 alike, and they count as
# the same fix (wave H, G-P2): ``'string'`` -- the alias every ``StringDtype``,
# the default ``str`` one included, compares equal to -- in a comparison or in
# the ``select_dtypes`` include list, and ``is_string_dtype(...)``; so does
# ``isinstance(..., StringDtype)``. A bare ``== 'object'`` -- as an assignment
# value, a boolean arm next to something that admits no string dtype, or a
# Series mask in a subscript -- is blind and flagged. ``category`` is deliberately none of this rule's business: it
# compared unequal to ``'object'`` on pandas 2 and still does, so sweeping it in
# would be a behaviour change, not a migration.
#
# ctx carries no parent map, so the analysis is done once, when the walk hands
# this rule the module node (ast.walk yields it first), and cached on ctx for
# the Compare and Call nodes that follow in the same file.

def _eq_object(node):
    """True for a ``<x> == 'object'`` comparison (literal on either side)."""
    if not isinstance(node, ast.Compare):
        return False
    if len(node.ops) != 1 or not isinstance(node.ops[0], ast.Eq):
        return False
    for operand in [node.left] + list(node.comparators):
        if const_value(operand, None) == "object":
            return True
    return False


#: The dtype names that select pandas 3's default ``str`` dtype: its own name,
#: and ``'string'``, which every ``StringDtype`` compares equal to.
_STR_NAMES = ("str", "string")


def _eq_str(node):
    """True for a ``<x> == 'str'`` / ``<x> == 'string'`` comparison (literal on
    either side)."""
    if not isinstance(node, ast.Compare):
        return False
    if len(node.ops) != 1 or not isinstance(node.ops[0], ast.Eq):
        return False
    for operand in [node.left] + list(node.comparators):
        if const_value(operand, None) in _STR_NAMES:
            return True
    return False


def _names_string_dtype(node):
    """``StringDtype`` itself, ``pd.StringDtype``, or a call of either."""
    if isinstance(node, ast.Call):
        node = node.func
    name = dotted(node)
    return bool(name) and name.split(".")[-1] == "StringDtype"


def _asks_string_dtype(node):
    """True for ``is_string_dtype(...)``, however it is reached, and for
    ``isinstance(..., StringDtype)`` (alone or in a tuple of classes)."""
    if not isinstance(node, ast.Call):
        return False
    name = dotted(node.func)
    if name and name.split(".")[-1] == "is_string_dtype":
        return True
    if name == "isinstance" and len(node.args) == 2:
        target = node.args[1]
        elts = (target.elts if isinstance(target, (ast.Tuple, ast.List))
                else [target])
        return any(_names_string_dtype(e) for e in elts)
    return False


def _tests_str(subtree):
    """True if a test that admits the ``str`` dtype appears under *subtree*:
    ``== 'str'``, ``== 'string'``, ``is_string_dtype(...)`` or
    ``isinstance(..., StringDtype)``."""
    for sub in ast.walk(subtree):
        if _eq_str(sub) or _asks_string_dtype(sub):
            return True
    return False


def _paired_with_str(cmp_node, parents):
    """True when *cmp_node* is an arm of an or/| group that also tests 'str'."""
    par = parents.get(id(cmp_node))
    if isinstance(par, ast.BoolOp) and isinstance(par.op, ast.Or):
        siblings = list(par.values)
    elif isinstance(par, ast.BinOp) and isinstance(par.op, ast.BitOr):
        siblings = [par.left, par.right]
    else:
        return False
    for sib in siblings:
        if sib is cmp_node:
            continue
        if _tests_str(sib):
            return True
    return False


def _select_dtypes_arg(node):
    """The ``include=`` argument node of a ``select_dtypes(...)`` call, or None."""
    if not isinstance(node, ast.Call):
        return None
    if not isinstance(node.func, ast.Attribute) or node.func.attr != "select_dtypes":
        return None
    kw = call_keyword(node, "include")
    if kw is not None:
        return kw.value
    return node.args[0] if node.args else None


@rule("pandas-object-dtype-blind", 6,
      "detects text columns by 'object' alone",
      "pandas 3 makes 'str' the default string dtype (PDEP-14), so a dtype "
      "checked against 'object' without also admitting 'str' no longer matches "
      "string columns. Exempt once the str dtype joins the check (a == 'object' "
      "or a == 'str' / == 'string' / is_string_dtype(a) / isinstance(a, "
      "StringDtype), (d == 'object') | (d == 'str'), x in ('object', 'str'), or "
      "select_dtypes(include=['object', 'str']) or ['object', 'string']).",
      (ast.Module, ast.Compare, ast.Call))
def _object_dtype_blind(node, ctx):
    if isinstance(node, ast.Module):
        parents = {}
        for parent in ast.walk(node):
            for child in ast.iter_child_nodes(parent):
                parents[id(child)] = parent
        blind = set()
        for sub in ast.walk(node):
            if _eq_object(sub) and not _paired_with_str(sub, parents):
                blind.add(id(sub))
        ctx.object_blind = blind
        return None
    if isinstance(node, ast.Compare):
        if id(node) in getattr(ctx, "object_blind", ()):
            return "== 'object'"
        return None
    if isinstance(node, ast.Call):
        arg = _select_dtypes_arg(node)
        if arg is None:
            return None
        if isinstance(arg, (ast.List, ast.Tuple, ast.Set)):
            vals = [const_value(e, None) for e in arg.elts]
            admits_str = (any(v in _STR_NAMES for v in vals)
                          or any(_names_string_dtype(e) for e in arg.elts))
            if "object" in vals and not admits_str:
                return "select_dtypes(include=[...'object'...])"
        elif const_value(arg, None) == "object":
            return "select_dtypes(include='object')"
    return None


# --------------------------------------------------------------------------
# step 7 -- Matplotlib 3.10, seaborn 0.13

@rule("seaborn-plt", 2,
      "reaches pyplot through seaborn",
      "sns.plt was a re-export seaborn dropped in 0.9, so it raises "
      "AttributeError under this step's seaborn 0.11.2 and every seaborn "
      "above it. Declared at step 2 rather than with the rest of the plotting "
      "rules because this one is not a matter of style: the two call sites in "
      "skutil/utils/util.py are broken code the moment the pin moves.",
      (ast.Attribute,))
def _sns_plt(node, ctx):
    # Matched on the ``plt`` attribute itself rather than anywhere in the
    # chain, so ``sns.plt.show()`` is one finding and not two.
    if node.attr != "plt":
        return None
    q = _q(ctx, node)
    if q == "seaborn.plt":
        return q
    return None


@rule("pyplot-show-in-library", 7,
      "calls plt.show() from library code",
      "A library that calls show() decides for its caller that a window "
      "opens. The helpers have to hand back a figure instead.",
      (ast.Call,), library_only=True)
def _plt_show(node, ctx):
    q = _q(ctx, node.func)
    if q in ("matplotlib.pyplot.show", "pylab.show"):
        return q
    return None


# Only the calls that act on *whichever* figure pyplot last touched.
# plt.figure(), plt.subplots() and plt.close() are creation and cleanup: they
# hand back the object they made, and using that object is the migrated
# spelling. Gating them would flag the fix along with the defect.
_PYPLOT_GLOBAL = frozenset("""
subplot gca gcf clf cla draw hold subplots_adjust tight_layout title xlabel
ylabel xticks yticks legend colorbar axis suptitle
""".split())


@rule("pyplot-global-state", 7,
      "drives the pyplot global figure stack from library code",
      "plt.gca()/plt.title()/plt.legend() all act on whichever figure pyplot "
      "last touched, so two helpers called in one process draw on each "
      "other's axes. The Figure/Axes API is the same drawing with an explicit "
      "receiver.",
      (ast.Call,), library_only=True)
def _pyplot_global(node, ctx):
    q = _q(ctx, node.func)
    if q is None:
        return None
    head, _, tail = q.rpartition(".")
    if head in ("matplotlib.pyplot", "pylab") and tail in _PYPLOT_GLOBAL:
        return q
    return None


@rule("matplotlib-ambient-rcparams", 7,
      "mutates global rcParams from library code",
      "An rcParams write outlives the call that made it and restyles every "
      "figure the process draws afterwards.",
      (ast.Call, ast.Assign), library_only=True)
def _rcparams(node, ctx):
    if isinstance(node, ast.Call):
        q = _q(ctx, node.func)
        if q in ("matplotlib.rc", "matplotlib.rcdefaults", "matplotlib.style.use",
                 "matplotlib.pyplot.rc", "matplotlib.pyplot.rcdefaults",
                 "seaborn.set", "seaborn.set_style", "seaborn.set_theme",
                 "seaborn.set_context"):
            return q
        if isinstance(node.func, ast.Attribute) and node.func.attr == "update":
            q2 = _q(ctx, node.func.value)
            if q2 in ("matplotlib.rcParams", "matplotlib.pyplot.rcParams"):
                return "rcParams.update"
        return None
    for tgt in node.targets:
        if isinstance(tgt, ast.Subscript):
            q2 = _q(ctx, tgt.value)
            if q2 in ("matplotlib.rcParams", "matplotlib.pyplot.rcParams"):
                return "rcParams[...] = ..."
    return None


# --------------------------------------------------------------------------
# step 8 -- vectorisation of the hot paths

# The three scans § 28 names, by the function that owns each. Named rather
# than pattern-matched because "transform" alone is thirteen methods in this
# package and only one of them is quadratic.
HOT_PATHS = ("filter_collinearity",
             "InteractionTermTransformer.transform",
             "GainsStatisticalReport.as_data_frame")

_ROW_ITER_METHODS = frozenset("iterrows itertuples".split())

# Every construct that steps through a sequence one element at a time in the
# interpreter. ``while`` is here too: the greedy drop in filter_collinearity is
# the one legitimate ``while`` in the three paths, and it is exempted by name
# below; any other ``while`` in a hot path is a row loop.
_LOOP_NODES = (ast.For, ast.While, ast.ListComp, ast.SetComp,
               ast.GeneratorExp, ast.DictComp)

# numpy's own per-element Python loops. np.vectorize / np.frompyfunc advertise
# themselves as vectorised but call a Python callable once per element -- inside
# a hot path they are a row loop wearing a numpy name, and agents reach for them
# believing they vectorise. Charged only inside the hot paths (the reference
# golden vectorises with real whole-array ops and uses neither), so this does
# not touch the package-wide count. getattr-spelled forms resolve through
# _dyn_attr, the same door the RNG/retired-name rules already close.
_HOT_DISGUISE = frozenset("numpy.vectorize numpy.frompyfunc".split())


def _parents(ctx):
    """id(child) -> parent-node for the whole module, built once per Ctx.

    An AST node carries no link to its parent, so a rule handed a single node
    cannot walk upward on its own. The map is materialised from ``ctx.tree`` on
    first use and cached, so the ancestry test below is O(depth), not O(n) per
    call.
    """
    cached = getattr(ctx, "_parents_cache", None)
    if cached is not None:
        return cached
    parents = {}
    if getattr(ctx, "tree", None) is not None:
        for parent in ast.walk(ctx.tree):
            for child in ast.iter_child_nodes(parent):
                parents[id(child)] = parent
    ctx._parents_cache = parents
    return parents


def _loop_ancestor(node, ctx):
    """True if a For/While/comprehension encloses *node* within its function.

    Walks parents until it reaches the function (or class/lambda) that owns the
    node -- so a ``while`` that sits directly in the function body has no loop
    ancestor, while one nested inside another loop does.
    """
    parents = _parents(ctx)
    cur = parents.get(id(node))
    while cur is not None:
        if isinstance(cur, (ast.FunctionDef, ast.AsyncFunctionDef,
                            ast.Lambda, ast.ClassDef)):
            return False
        if isinstance(cur, _LOOP_NODES):
            return True
        cur = parents.get(id(cur))
    return False


def _for_iter_desc(node, ctx):
    """A short label for what a ``for`` walks -- for the finding text only.

    The charge does not depend on the spelling any more (every ``for`` in a hot
    path counts); this only chooses a readable ``what``.
    """
    it = node.iter
    if isinstance(it, ast.Call):
        q = _q(ctx, it.func)
        if q in ("range", "builtins.range", "xrange"):
            return "for ... in range(...)"
        if isinstance(it.func, ast.Attribute) and \
                it.func.attr in _ROW_ITER_METHODS:
            return "for ... in ." + it.func.attr + "()"
        if q in ("enumerate", "builtins.enumerate", "zip", "builtins.zip"):
            return "for ... in " + q.split(".")[-1] + "(...)"
    return "for ... in <sequence>"


@rule("hot-path-row-iteration", 8,
      "iterates in the interpreter inside a hot path",
      "The three O(n^2)/row-wise scans this step targets each compute a "
      "rectangular result and must do it with whole-array operations, not an "
      "interpreter loop. Inside filter_collinearity, "
      "InteractionTermTransformer.transform and "
      "GainsStatisticalReport.as_data_frame every `for` and every comprehension "
      "counts, whatever it walks: range, enumerate, zip, .iterrows()/"
      ".itertuples(), a bare df.columns, a np.stack of index pairs or a "
      "pre-built pair list are the same element walk under different spellings. "
      "Every `while` counts too, with one exception: the greedy drop in "
      "filter_collinearity removes one column per pass (not one per data "
      "element) and is inherently sequential, so its single top-level `while` "
      "-- and only that one -- is allowed; a `while` nested in another loop, or "
      "any `while` in the other two functions, is a row loop and counts. One "
      "finding per construct, so a nested pair of loops counts twice. "
      "np.vectorize and np.frompyfunc count too: they are a per-element Python "
      "loop wearing a numpy name, not a whole-array op.",
      (ast.For, ast.While, ast.ListComp, ast.SetComp, ast.GeneratorExp,
       ast.DictComp, ast.Call),
      library_only=True, functions=HOT_PATHS)
def _hot_row_iter(node, ctx):
    if isinstance(node, ast.Call):
        # np.vectorize / np.frompyfunc are a per-element Python loop wearing a
        # numpy name; inside a hot path they are row iteration, direct or
        # getattr-spelled. No other call counts here (the loop nodes carry the
        # rest), so real whole-array numpy in the golden stays uncharged.
        q = _q(ctx, node.func)
        if q in _HOT_DISGUISE:
            return q.split(".")[-1] + "(...)"
        dyn = _dyn_attr(ctx, node)
        if dyn in _HOT_DISGUISE:
            return dyn.split(".")[-1] + "(...) via getattr"
        return None
    if isinstance(node, ast.While):
        owner = ctx.owner.get(node, "") or ""
        # Only the module-level filter_collinearity's own top-level greedy loop
        # is the allowed while. Match the owner exactly, not by tail name: a
        # nested ``def filter_collinearity`` inside transform/as_data_frame must
        # NOT inherit the exemption, and a while in a helper nested inside the
        # real filter_collinearity (owner ``filter_collinearity._scan``) already
        # fails this test and counts.
        if owner == "filter_collinearity" and not _loop_ancestor(node, ctx):
            return None
        return "while <row/element loop>"
    if isinstance(node, ast.For):
        return _for_iter_desc(node, ctx)
    return "comprehension"


@rule("pandas-row-iteration", 8,
      "iterates a frame row by row",
      "iterrows/itertuples anywhere in the package: this step's property is "
      "that the row loop is gone, not that it moved to another function.",
      (ast.Attribute,), library_only=True)
def _row_iteration(node, ctx):
    if node.attr in ("iterrows", "itertuples"):
        return node.attr
    return None


# The row-wise axis, every spelling pandas accepts for it: the integer 1 and the
# string "columns". axis=0/"index" is column-wise (a real reduction) and is not
# charged; pandas rejects any other value, so there is nothing else to spell.
_ROW_APPLY_AXIS = (1, "columns")


@rule("pandas-apply-axis-1", 8,
      "applies a Python callable across rows",
      "df.apply(f, axis=1) is a Python-level row loop wearing a vectorised "
      "coat: it calls f once per row. axis=\"columns\" is the same row axis "
      "under pandas' string spelling and counts the same; axis=0/\"index\" is "
      "column-wise and does not.",
      (ast.Call,), library_only=True)
def _apply_axis1(node, ctx):
    if not isinstance(node.func, ast.Attribute):
        return None
    if node.func.attr not in ("apply", "applymap", "map"):
        return None
    kw = call_keyword(node, "axis")
    if kw is not None and const_value(kw.value, None) in _ROW_APPLY_AXIS:
        return "apply(axis=%r)" % (const_value(kw.value, None),)
    for arg in node.args[1:]:
        if const_value(arg, None) in _ROW_APPLY_AXIS:
            return "apply(..., row axis)"
    return None


# polars / duckdb per-row Python callbacks. D-7 leaves the vectorisation engine
# open, so the "no row iteration" property has to hold whichever engine a hot
# path reaches for. A Python function invoked once per row is a row loop wearing
# an engine method's name: polars Series.map_elements / DataFrame.map_rows /
# apply_rows and duckdb's create_function each call a Python callable per row.
# (map_batches is whole-column and is deliberately NOT listed.) Zero on the
# reference golden and on the floor, both of which vectorise with numpy.
_ENGINE_ROW_UDF = frozenset(
    "map_elements map_rows apply_rows create_function".split())


@rule("hot-path-engine-udf", 8,
      "runs a Python callable once per row through a columnar engine",
      "A per-row Python callback pushed into polars or duckdb is still a row "
      "loop: polars map_elements / map_rows / apply_rows and duckdb "
      "create_function invoke a Python function once per row. Vectorise; do not "
      "smuggle the loop into an engine UDF.",
      (ast.Call,), library_only=True)
def _engine_row_udf(node, ctx):
    # method spelling: frame.map_elements(fn), con.create_function("f", fn)
    if isinstance(node.func, ast.Attribute) and \
            node.func.attr in _ENGINE_ROW_UDF:
        return node.func.attr + "(...)"
    # bare-name spelling: `from duckdb import create_function; create_function(fn)`
    # -- the same call, imported unqualified.
    if isinstance(node.func, ast.Name) and node.func.id in _ENGINE_ROW_UDF:
        return node.func.id + "(...)"
    # getattr spelling: getattr(con, "create_function")(fn). The object may be a
    # connection or a frame, not a module, so _dyn_attr cannot resolve it; read
    # the literal name off the getattr directly -- same door the RNG/retired
    # rules close, applied to the object case they do not reach.
    if _q(ctx, node.func) == "getattr" and len(node.args) >= 2:
        name = const_value(node.args[1], None)
        if isinstance(name, str) and name in _ENGINE_ROW_UDF:
            return name + "(...) via getattr"
    return None


# --------------------------------------------------------------------------
# step 8 -- persistence a reader can trust
#
# Every serializer built on pickle: a file it wrote executes code when it is
# loaded, and is bound to the exact class layout of the library versions that
# wrote it. Matched on the resolved callee, so `from joblib import dump`,
# `import joblib as jl` and `getattr(joblib, "dump")` are all the same call.
# The replacement half -- that the save and the load still happen, through
# skops.io -- is not a construct a rule can count down; `scan.call_census`
# counts it and the manifest row of kind `replaced` combines the two.
_PICKLE_CALLS = frozenset("""
pickle.dump pickle.dumps pickle.load pickle.loads pickle.Pickler
pickle.Unpickler _pickle.dump _pickle.dumps _pickle.load _pickle.loads
_pickle.Pickler _pickle.Unpickler
joblib.dump joblib.load
sklearn.externals.joblib.dump sklearn.externals.joblib.load
cloudpickle.dump cloudpickle.dumps cloudpickle.load cloudpickle.loads
dill.dump dill.dumps dill.load dill.loads
pandas.read_pickle pandas.to_pickle
shelve.open
""".split())


@rule("pickle-persistence", 8,
      "persists or restores an object through pickle",
      "pickle -- and joblib, cloudpickle, dill, shelve and pandas' "
      "read_pickle/to_pickle, all built on it -- executes code when a file is "
      "loaded and binds the file to the class layout of the versions that wrote "
      "it. A fitted model is persisted with skops.io instead, whose files carry "
      "a versioned schema and load without executing code. Matched on the "
      "resolved callee, so an alias, a from-import or a getattr fetch is the "
      "same call; `.to_pickle(...)` is matched as a method on anything, and "
      "numpy.load only when it is asked to unpickle.",
      (ast.Call,))
def _pickle_persistence(node, ctx):
    q, runtime = _rng_target(ctx, node)
    if q in _PICKLE_CALLS:
        return _said(q, runtime)
    if isinstance(node.func, ast.Attribute) and node.func.attr == "to_pickle":
        return ".to_pickle(...)"
    if q in ("numpy.load", "numpy.lib.npyio.load"):
        kw = call_keyword(node, "allow_pickle")
        if kw is not None and const_value(kw.value, None) is True:
            return q + "(allow_pickle=True)"
    return None
