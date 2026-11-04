"""Which scenarios exist, how many ports each needs, and where each is scored.

`steps` is the set of steps whose verifier includes the scenario. Behaviour
scenarios are scored from step 1 onward (they are the regression contract);
a scenario that exercises a semantic the tree only acquires at step k starts at
step k. `flip_from` marks a scenario whose expectations are re-recorded from
the golden tree of the step it names, the documented-change exception
`flips.py` describes.

`baseline_dir` names a different recording of the untouched tree for the
purpose of deciding what is progress. It exists for one scenario. The baseline
corpus is recorded from a tree the recorder had to help off the ground -- the
2016 source does not compile on this decade's toolchain, which is the first
half of step 1 -- and every behaviour observation needs that help, because a
tree that produced no binaries answers no questions at all. The build gate is
the one scenario that is *about* that, so measuring it against a tree that was
given the crutches makes the step's headline requirement indistinguishable from
the baseline and therefore worth no progress at all. It is measured against the
pin as the pin is instead.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import norm  # noqa: E402

ALL_STEPS = (1, 2, 3, 4, 5, 6)


def _s(*a):
    return set(a)


SCENARIOS = [
    # name, ports, family, steps scored in, re-record source (None = baseline)
    dict(name="c01_strings", ports=1, family="behaviour", steps=ALL_STEPS),
    dict(name="c02_containers", ports=1, family="behaviour", steps=ALL_STEPS),
    dict(name="c03_keyspace", ports=1, family="behaviour", steps=ALL_STEPS),
    dict(name="c04_scripting_tx", ports=1, family="behaviour", steps=ALL_STEPS),
    dict(name="c05_protocol", ports=1, family="behaviour", steps=ALL_STEPS),
    dict(name="c06_server_admin", ports=2, family="behaviour", steps=ALL_STEPS),
    dict(name="c07_persistence", ports=2, family="behaviour", steps=ALL_STEPS),
    dict(name="c08_listeners", ports=8, family="behaviour", steps=ALL_STEPS),
    dict(name="c09_cli", ports=2, family="behaviour", steps=ALL_STEPS),
    dict(name="c10_replication", ports=4, family="behaviour", steps=ALL_STEPS),
    dict(name="c11_cluster", ports=8, family="behaviour", steps=ALL_STEPS),
    dict(name="c12_sentinel", ports=8, family="behaviour", steps=ALL_STEPS),
    dict(name="c13_benchmark", ports=2, family="behaviour", steps=ALL_STEPS),
    # The Tcl suite manages its own ports (11111, 21111+), so it takes no block.
    dict(name="c14_tclsuite", ports=0, family="behaviour", steps=ALL_STEPS),
    dict(name="c15_cli_modes", ports=2, family="behaviour", steps=ALL_STEPS),
    dict(name="c16_sentinel_failure", ports=6, family="behaviour",
         steps=ALL_STEPS),
    dict(name="c17_cluster_failover", ports=6, family="behaviour",
         steps=ALL_STEPS),
    # Step-specific behaviour. Each of these records a semantic the tree only
    # acquires at `flip_from`, so its expectations come from that step's golden
    # tree rather than from the baseline -- the documented-change
    # exception. Each is designed to hold still from that step onward: whatever
    # a later step changes again belongs in the later step's scenario, not here.
    dict(name="d01_resolution", ports=6, family="behaviour", steps=_s(1, 2, 3, 4, 5, 6),
         flip_from=1),
    dict(name="d02_happy_eyeballs", ports=10, family="behaviour", steps=_s(2, 3, 4, 5, 6),
         flip_from=2),
    dict(name="d03_address_text", ports=10, family="behaviour", steps=_s(3, 4, 5, 6),
         flip_from=3),
    dict(name="d04_no_ipv4", ports=12, family="behaviour", steps=_s(4, 5, 6),
         flip_from=4),
    dict(name="d05_scoped", ports=8, family="behaviour", steps=_s(5, 6), flip_from=5),
    dict(name="d06_v6_default", ports=6, family="behaviour", steps=_s(6), flip_from=6),
    # Migrated-ness gates. A gate measures a property the baseline fails by
    # construction, so every one of them is recorded from the golden tree of the
    # step that introduces the property.
    dict(name="g01_build", ports=0, family="gate", steps=ALL_STEPS, flip_from=1,
         baseline_dir="expectations-pin"),
    dict(name="g02_family_literals", ports=0, family="gate", steps=ALL_STEPS,
         flip_from=1),
    dict(name="g03_syscall_audit", ports=4, family="gate", steps=ALL_STEPS,
         flip_from=1),
    dict(name="g04_attempt_timing", ports=7, family="gate", steps=_s(2, 3, 4, 5, 6),
         flip_from=2),
    dict(name="g05_canonical_text", ports=6, family="gate", steps=_s(3, 4, 5, 6),
         flip_from=3),
    dict(name="g06_no_ipv4_kernel", ports=8, family="gate", steps=_s(4, 5, 6),
         flip_from=4),
    dict(name="g07_address_classes", ports=4, family="gate", steps=_s(5, 6),
         flip_from=5),
    dict(name="g08_ipv4_free_artifacts", ports=5, family="gate", steps=_s(6),
         flip_from=6),
    # Step 5's refusals on the paths where an address arrives from another
    # machine: gossip, a master's INFO and hello messages. Last in the list, so
    # every port block above keeps the base it was recorded with.
    dict(name="d05_learned", ports=22, family="behaviour", steps=_s(5, 6),
         flip_from=5),
]

# Assign a disjoint port block to every scenario, deterministically.
_p = norm.BASE_PORT
for _sc in SCENARIOS:
    _sc["base_port"] = _p
    _p += max(_sc["ports"], 1)
assert _p <= norm.BASE_PORT + norm.PORT_SPAN, "port span exhausted: %d" % _p

BY_NAME = {s["name"]: s for s in SCENARIOS}


def for_step(step):
    return [s for s in SCENARIOS if step in s["steps"]]


def recorded_from(step):
    """Scenarios whose expectations are recorded from step `step`'s tree.

    `step` 0 means the untouched baseline. Everything without a `flip_from` is
    baseline-recorded and stays that way for every step it is scored in.
    """
    return [s for s in SCENARIOS if s.get("flip_from", 0) == step]
