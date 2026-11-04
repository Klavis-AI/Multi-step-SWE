"""skutil.base and skutil.utils.metaestimators: the decorators everything else
is built out of.

``overrides`` asserts at class-definition time, ``since`` rewrites a docstring
by measuring its indentation, ``suppress_warnings`` swallows a warning, and the
two delegate descriptors decide at attribute-access time whether a method
exists at all. None of it touches NumPy or pandas -- which is exactly why it
belongs in the recorded suite: it must come through the migration unchanged.
"""
from __future__ import print_function, absolute_import, division

import numpy as np
import pandas as pd

from ..registry import case, adversarial, BEHAVIOR, ADVERSARIAL
from .. import data


def _bind(fn, *args, **kwargs):
    def run():
        return fn(*args, **kwargs)
    return run


def _overrides_cases():
    from skutil.base import overrides

    class A(object):
        def a(self):
            return 1

    def good():
        class B(A):
            @overrides(A)
            def a(self):
                return 2
        return B().a()

    case("base.overrides.valid", tags=(BEHAVIOR,))(good)

    def bad():
        class C(A):
            @overrides(A)
            def b(self):
                return 1
        return C

    adversarial("base.overrides.not_a_super_method")(bad)

    def dunder():
        """``dir`` includes inherited dunders, so overriding ``__repr__`` of
        ``object`` passes the assertion."""
        class D(A):
            @overrides(A)
            def __repr__(self):
                return "D()"
        return repr(D())

    case("base.overrides.dunder", tags=(BEHAVIOR, ADVERSARIAL))(dunder)


_overrides_cases()


def _since_cases():
    from skutil.base import since

    def flat():
        @since("0.1.5")
        def f():
            """Some docstring"""
            return None
        return f.__doc__

    case("base.since.flat_docstring", tags=(BEHAVIOR,))(flat)

    def indented():
        @since(0.2)
        def f():
            """Header.

            Body line.
            """
            return None
        return f.__doc__

    case("base.since.indented_docstring", tags=(BEHAVIOR,))(indented)

    def numeric_version():
        @since(3)
        def f():
            """Doc"""
            return None
        return f.__doc__

    case("base.since.numeric_version", tags=(BEHAVIOR,))(numeric_version)

    def no_docstring():
        @since("0.1.5")
        def f():
            return None
        return f.__doc__

    adversarial("base.since.missing_docstring")(no_docstring)


_since_cases()


def _suppress_cases():
    from skutil.base import suppress_warnings
    import warnings

    @suppress_warnings
    def warns():
        warnings.warn("This is a warning", UserWarning)
        return 1

    case("base.suppress_warnings.swallows", tags=(BEHAVIOR,))(warns)

    @suppress_warnings
    def raises():
        warnings.warn("still going", UserWarning)
        raise ValueError("a warning filter is not an exception handler")

    adversarial("base.suppress_warnings.does_not_swallow_errors")(raises)


_suppress_cases()


def _baseskutil_cases():
    from skutil.base import BaseSkutil, SelectiveMixin

    class A(BaseSkutil):
        def __init__(self, cols=None, as_df=None):
            super(A, self).__init__(cols, as_df)

    case("base.BaseSkutil.repr", tags=(BEHAVIOR,))(lambda: repr(A()))
    case("base.BaseSkutil.get_params", tags=(BEHAVIOR,))(
        lambda: A(cols=["a"], as_df=False).get_params())
    case("base.BaseSkutil.set_params", tags=(BEHAVIOR,))(
        lambda: A().set_params(cols=["b", "c"]).get_params())
    case("base.BaseSkutil.is_selective", tags=(BEHAVIOR,))(
        lambda: isinstance(A(), SelectiveMixin))
    adversarial("base.BaseSkutil.unknown_param")(
        lambda: A().set_params(nope=1))
    adversarial("base.BaseSkutil.abstract_instantiation")(
        lambda: BaseSkutil())

    def clone_roundtrip():
        from sklearn.base import clone
        a = A(cols=["a", "b"], as_df=True)
        b = clone(a)
        return {"params": b.get_params(), "distinct": a is not b}

    case("base.BaseSkutil.clone", tags=(BEHAVIOR,))(clone_roundtrip)


_baseskutil_cases()


def _delegate_cases():
    from skutil.utils.metaestimators import (if_delegate_has_method,
                                             if_delegate_isinstance)

    class Inner(object):
        def go(self, x):
            return x * 2

    class Other(object):
        pass

    def make_has_method(delegate, method=None, inner=Inner()):
        class Outer(object):
            def __init__(self):
                self.est = inner

            @if_delegate_has_method(delegate, method=method)
            def run(self, x):
                """Run the inner thing."""
                return self.est.go(x) + 1
        return Outer()

    case("base.if_delegate_has_method.string", tags=(BEHAVIOR,))(
        lambda: make_has_method("est", method="go").run(3))
    case("base.if_delegate_has_method.list", tags=(BEHAVIOR,))(
        lambda: make_has_method(["missing", "est"], method="go").run(3))
    case("base.if_delegate_has_method.tuple", tags=(BEHAVIOR,))(
        lambda: make_has_method(("missing", "est"), method="go").run(3))
    case("base.if_delegate_has_method.docstring", tags=(BEHAVIOR,))(
        lambda: make_has_method("est", method="go").run.__doc__)
    case("base.if_delegate_has_method.class_access", tags=(BEHAVIOR,))(
        lambda: type(make_has_method("est", method="go")).run.__doc__)
    adversarial("base.if_delegate_has_method.missing_attribute")(
        lambda: make_has_method("est", method="nope").run)
    adversarial("base.if_delegate_has_method.missing_delegate")(
        lambda: make_has_method("not_here", method="go").run)
    # the method name defaults to the decorated function's own name
    adversarial("base.if_delegate_has_method.name_defaults_to_fn")(
        lambda: make_has_method("est").run)
    case("base.if_delegate_has_method.name_matches_fn", tags=(BEHAVIOR,))(
        _named_delegate)

    def make_isinstance(delegate, tp, inner):
        class Outer(object):
            def __init__(self):
                self.est = inner

            @if_delegate_isinstance(delegate, tp)
            def run(self, x):
                """Run the typed thing."""
                return self.est.go(x) + 1
        return Outer()

    case("base.if_delegate_isinstance.match", tags=(BEHAVIOR,))(
        lambda: make_isinstance("est", Inner, Inner()).run(3))
    case("base.if_delegate_isinstance.list", tags=(BEHAVIOR,))(
        lambda: make_isinstance(["missing", "est"], Inner, Inner()).run(3))
    case("base.if_delegate_isinstance.tuple", tags=(BEHAVIOR,))(
        lambda: make_isinstance(("missing", "est"), Inner, Inner()).run(3))
    case("base.if_delegate_isinstance.docstring", tags=(BEHAVIOR,))(
        lambda: make_isinstance("est", Inner, Inner()).run.__doc__)
    adversarial("base.if_delegate_isinstance.wrong_type")(
        lambda: make_isinstance("est", Other, Inner()).run)
    adversarial("base.if_delegate_isinstance.missing_delegate")(
        lambda: make_isinstance("not_here", Inner, Inner()).run)


def _named_delegate():
    """A delegate method whose name matches the decorated function's."""
    from skutil.utils.metaestimators import if_delegate_has_method

    class Inner(object):
        def run(self, x):
            return x - 1

    class Outer(object):
        def __init__(self):
            self.est = Inner()

        @if_delegate_has_method("est")
        def run(self, x):
            return self.est.run(x) * 10

    return Outer().run(3)


_delegate_cases()


def _package_cases():
    import skutil

    case("base.package.version", tags=(BEHAVIOR,))(lambda: skutil.__version__)
    case("base.package.all", tags=(BEHAVIOR,))(lambda: sorted(skutil.__all__))
    case("base.package.log_exp_at_top_level", tags=(BEHAVIOR,))(
        lambda: {"log": skutil.log(np.array([1.0, np.e])),
                 "exp": skutil.exp(np.array([0.0, 1.0]))})

    # There is deliberately no case here for ``skutil.setup_module``.
    #
    # It was one, and it had to go: it recorded the numbers that come out of the
    # global RNG *after* the nose-era hook reseeds it from an unseeded
    # ``np.random.uniform()``. That value is the defect, not a behaviour. Step 1
    # is required to stop the hook disturbing the caller's stream, and every way
    # of doing that -- deleting the hook, neutering it, saving and restoring
    # around it -- changes the recorded numbers. Leaving the case in would have
    # made the two scored families contradict each other: no submission could
    # satisfy the migrated-ness gate without failing behaviour equivalence, and
    # the step's own requirements would have made one of its fixtures
    # unpassable.
    #
    # The property is not lost, it moved to where it belongs: the
    # ``module_fixtures_preserve_rng`` check in ``skutil_gates.repro`` calls
    # every module-level fixture the package exposes and asserts that both
    # global streams are exactly where they were. That is a gate, and gates are
    # what the migration is scored on changing.


_package_cases()


