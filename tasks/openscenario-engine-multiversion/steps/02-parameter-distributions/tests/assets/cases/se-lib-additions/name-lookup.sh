#!/usr/bin/env bash
# An entity can be found by the name the scenario gives it, not only by the
# index it happens to occupy. Four answers from the same entry point: nothing
# before a scenario is loaded, the two entities the fixture declares, and a
# name that is not in it.
source /tests/caselib.sh

run_driver se_reports "$FIXTURES/param_cars.xosc" "$WORK/rec.dat" 20 0.1 \
    || fail "se_reports exited $?"

need_neg id_before "looking an entity up before any scenario is initialised"
need_kv init 0 "the fixture scenario initialises"
need_kv id_ego 0 "the id of the first declared entity, found by name"
need_kv id_target 1 "the id of the second declared entity, found by name"
need_neg id_unknown "a name that is not in the scenario"
# The name lookup and the existing index lookup have to agree about the same
# entity, or one of them is answering from a different table.
need_kv id_by_index_0 0 "the id of entity index 0, found the pre-existing way"
need_kv survived 1 "the engine survives the lookups"
