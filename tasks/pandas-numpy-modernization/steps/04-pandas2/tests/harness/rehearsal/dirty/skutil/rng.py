"""Step 1: process-global randomness and the collections ABC aliases."""

import collections
import importlib
import operator
import random
import sys
from collections import Iterable

import numpy as np


def draw(n):
    np.random.seed(7)                       # global-rng-seed
    values = np.random.randn(n)             # global-rng-draw
    random.shuffle(list(values))            # stdlib-global-rng
    return values


# The same two streams reached through a name assembled at runtime, which is
# what ruff cannot see at all and an AST rule sees only if it looks through the
# fetch. The second line is a stdlib global draw as well as an indirection.
MTRAND = importlib.import_module("numpy.random")   # dynamic-legacy-access, import arm
SHUFFLE = getattr(random, "shuffle")               # dynamic-legacy-access, getattr arm

# Three more ways to spell the same fetch, none of which contains a getattr or
# an import. They are here because the first version of the rule read none of
# them, and a gate that stops one spelling of an indirection has not stopped
# the indirection.
SYSMOD = sys.modules["numpy.random"]               # dynamic-legacy-access, sys.modules arm
RAND = np.random.__dict__["rand"]                  # dynamic-legacy-access, __dict__ arm
SEEDER = operator.attrgetter("seed")(np.random)    # dynamic-legacy-access, operator arm

SIZED = collections.Sized                   # collections-abc-alias, attribute
ITERABLE = Iterable                         # imported above: the import arm
