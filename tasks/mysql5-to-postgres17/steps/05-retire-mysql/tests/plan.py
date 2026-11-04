"""Step 05 -- MySQL is gone.

Step 04 moved the application onto PostgreSQL 17. It did not remove MySQL; it
could not, because the cutover is what a step-04 candidate is graded on and
the old server's data directory is what step 04's `behavior:mysql-intact`
guard required them to leave alone. So the tree this step is handed still
carries a MySQL adapter, a MariaDB adapter that subclasses it, three adapter
self-tests, seven MySQL-dialect installer files, twelve MySQL and Drizzle
files in the vendored Doctrine tree, eight driver sites, a live MySQL
fallback in the plugin that configures Doctrine, a registry that advertises
three MySQL back-ends, a data directory, a server configuration directory,
and a provisioning script that installs `mysql-server` and `phpmyadmin`.

None of it is on the live path. That is the difficulty of this step and the
reason its weights are shaped the way they are.

**Everything a name-based sweep can do is a guard here.** `find /app -iname
'*mysql*' -delete` breaks nothing -- after step 04 every MySQL file left in
this tree is dead code -- so on a plan that scored the file inventory, the
driver sites and the two directories, that sweep collects about 0.86 and rule
4.3's ceiling is ~30%. Reweighting cannot fix it: rule 5 puts a floor of ~30%
on the behaviour side and the sweep takes all of that for free too.

What fixes it is step 04's own lesson (§22, the `behavior:tables-untouched`
split): find the subset a mechanical pass gets for free and move it out of the
scored families into the guards, where it must still be true and earns
nothing.

"Mechanical pass" is read strictly, and the strict reading is what §24.14
cost this plan a second cut. `sed -i -E '/mysql|mariadb/Id'` over the four
wiring files is a mechanical pass, and it satisfies every *absence* question
this step can ask about them -- including all nineteen literals, which were
the scored family until that was measured. An absence question cannot be
scored here. So:

    migrated:inventory        guard   25 files gone, 165 still here
    migrated:drivers          guard   the one driver site that needs an edit;
                                      the other seven are in files the
                                      inventory deletes and are not counted
                                      twice
    migrated:server-retired   guard   the two directories that travel here
    migrated:postgres-intact  guard   the cluster those two must not take with
                                      them
    migrated:wiring-gone      guard   the nineteen string literals that still
                                      select a MySQL back-end
    migrated:wiring-intact    guard   the tree still reads; nothing invented,
                                      orphaned, or left pointing at a class
                                      that is gone
    migrated:provisioning     guard   vagrant_bootstrap.sh

and the one scored `migrated:` family is the one thing the line filter cannot
touch, because every check in it asks whether what is *left* still resolves:

    migrated:wiring       0.70  five checks: the Doctrine fallback names a
                                driver this tree implements, the adapter
                                harness imports test files that exist and
                                still has its surviving arm, and the
                                reserved-words default list names only
                                back-ends the registry still has

`migrated:provisioning` was scored at 0.24 until the ceiling was measured
rather than predicted. A fifty-line sed pass took all of it and read 0.527
overall; the requirement is one substitution table and a line filter, which is
what an automated migrator *is*. Demoted, the same pass reads 0.273 and the
name-sweep that does not touch the recipe reads 0.000, because the recipe is
now a guard it fails. NOTES §24.13 has both runs.

Rule 5's two kinds are behaviour 0.30 and migrated-ness 0.70. The untouched
step-04 tree passes the behaviour side completely -- those families are step
04's own, carried forward unchanged, including its flip list and its
overrides, because this step changes no response -- and scores 0/5 on
`migrated:wiring`. That 0.30 is the floor rule 5 sets and it is free to any
pass that breaks nothing, which is unavoidable in a step whose whole
behavioural requirement is "change no response". It is why the migrated side
carries 0.70 and why so much of this step is guards: the guard product, not
the earned weight, is what a half-done retirement runs into.

Four instruments, and why four. `driverscan.php` reports *uses* of a MySQL
driver and is blind to a class that survives its parent or a registry that
names a class that is gone; `classmap.php` (new here) reports class
declarations, inheritance edges, and the string literals that select a
back-end, which is what those two shapes are made of; `wiringmap.php` (§24.15)
reports the *structure* those literals sit in -- which labels a switch arm
carries and what that arm imports, what a guarded assignment assigns, what a
keyed array maps to what -- which is the only way to ask whether the thing
left behind still points at something; `retire.provisioning` reads shell
words. NOTES-authoring.md §24.3 has the measurement that decides the literal
question -- 32 watched literals over 905 files, none of them prose, while a
grep for the same words hits twelve Doctrine files that say "MySQL" in a
sentence and must not be touched.

**Order.** Unchanged from step 04 except at the ends. The seven retirement
families that only read the tree off disk run first, before anything has
connected to a database, because nothing they do can disturb what follows and
a report that names the retirement failures is useful even when the
deployment will not answer. Then the five that read the delivered database as
the authority, then the single corpus pass and its readings, then
`migrated:dialect`, then the three install families last, because they are the
only thing left that a tree could still damage.
"""

from wglib import (behavior, behaviorgate, datagate, dbstate, gate, mysqlgone,
                   pglog, pgnames, pgstep, retire)
STEP = "05"
TITLE = "MySQL is gone"
WANT_HTTP = True
ROOTS = ("/app",)

# Step 04's flip list and overrides, carried forward under their own names
# because they are the same recording: this step asks for no behavioural
# change, so the responses it expects are exactly the ones step 04 left. A
# second copy under a `-05` name would be a second thing to keep in step with
# the first. `authoring/sync_tests.py` merges `authoring/fixtures/` and
# `authoring/fixtures-steps/05-retire-mysql/` into one `tests/fixtures/`.
FLIPS = "/tests/fixtures/behavior-flips-04.json"
OVERRIDES = "/tests/fixtures/behavior-overrides-04.json"

# Read once, at import, before any family has read a database. `datagate`
# asserts that nothing has: the authority is a property of the plan, not
# something a family may change while others are running. The reasoning is in
# "Where the data's authority went" above.
datagate.use_delivered_postgres()

# How the single corpus pass is taken -- also the plan's decision and not
# whichever family reaches it first, since the pass is cached and shared.
#
# `strict=False`: a deployment that stops answering partway is graded on what
# it did answer, request by request, instead of the recorder raising and
# taking all four behaviour families down with it.
#
# `logmark`: the recorder notes where the PostgreSQL log stood at each
# scenario boundary, which is what lets `migrated:runtime` attribute a logged
# statement to the scenario that caused it without driving the corpus twice.
behaviorgate.configure(strict=False, logmark=pglog.mark)


def _state():
    """The delivered PostgreSQL, read the way the recording was written.

    The recording's 24 comparable tables are resolved by name against whatever the
    candidate's `search_path` reaches, for the reason `datagate.states()`
    gives: step 02 left a migration free to fold the four mixed-case table
    names or to quote them, and free to put the schema where it likes. A gate
    that looked for `lesson_XSS2_messages` in `public` would fail a tree the
    schema gate passed.

    `binary_columns` is left at its default rather than passed, because that
    is the reading the two golden corpus passes were scored with (§22) and a
    snapshot instrument that is configured differently from the one the answer
    was measured on is not the same instrument.
    """
    conn, note = pgstep.connection()
    if conn is None:
        raise RuntimeError(note)
    expected = behavior.load(behavior.FIXTURE)
    want = sorted(set(expected["tables_before"]) | set(expected["tables_after"]))
    pgnames.widen_search_path(conn)
    return dbstate.PostgresState(conn, resolved=pgnames.resolve_tables(conn, want))


# Step 03's widening of `schemagate.TYPE_MAP`, carried forward unchanged. It
# is not this step's decision and it is not optional: `jf_options.Value` holds
# 96 NUL bytes and step 03 delivered it as `bytea`, so a step 04 that asked the
# schema families the unwidened question would fail four guards on the tree it
# handed the candidate itself. Copied rather than imported because a step's
# `tests/` directory is its whole build context (`sync_tests.py`) and cannot
# reach another step's.
EXTRA_COLUMNS = {
    ("jf_options", "Value"): {"bytea"},
    ("jf__test_options", "Value"): {"bytea"},
}


def _widened(fn):
    """A schema-reading family, asked with this step's column widening."""
    def wrapper(ctx=None):
        return fn(ctx, extra_columns=EXTRA_COLUMNS)
    return wrapper


def _corpus(name, kinds):
    """One behaviour family over every recorded id of those kinds.

    No `subset` and no `precondition`. The partition steps 01-03 carry exists
    because strict mode moved nine responses that the rest of the corpus
    reproduced for free; nothing about this step reproduces anything for free,
    since the server the recording was taken from is not in the image. And the
    precondition those steps pass -- `strictgate.precondition` -- is a MySQL
    reading, which is the same thing this step's plan retires three families
    over.
    """
    def fn(ctx):
        return behaviorgate.checks(ctx, name, _state, kinds=kinds,
                                   subset="all", precondition=None,
                                   flips_path=FLIPS, overrides_path=OVERRIDES)
    return fn


_RUNTIME = {}


def _runtime(which):
    """What the application sent to PostgreSQL, out of the server's own log.

    Shares the single corpus pass every behaviour family takes and reads the
    byte windows the recorder marked in it, once for both families. A pass
    that could not be taken at all fails both whole denominators exactly as it
    does there: the questions are fixed before the tree is looked at, and a
    deployment that cannot be driven has answered none of them.
    """
    def fn(ctx):
        if not _RUNTIME:
            obs, error = behaviorgate.observation(ctx, _state)
            if error is not None:
                _RUNTIME.update(zip(
                    pgstep.RUNTIME_FAMILIES,
                    [gate.crashed(pgstep.RUNTIME_FAMILIES[0],
                                  "the corpus could not be replayed: %s"
                                  % error, pgstep.n_runtime_identity()),
                     gate.crashed(pgstep.RUNTIME_FAMILIES[1],
                                  "the corpus could not be replayed: %s"
                                  % error, pgstep.n_runtime_sql())]))
            else:
                _RUNTIME.update(zip(
                    pgstep.RUNTIME_FAMILIES,
                    pgstep.runtime_families(
                        windows=obs.get("log_windows") or ())))
        return _RUNTIME[which]
    return fn



# ------------------------------------------------------------- retirement ---

DRIVERS = "/tests/fixtures/drivers-step05.json"
WIRING = "/tests/fixtures/wiring-step05.json"
INVENTORY = "/tests/fixtures/inventory-step05.json"


def _drivers(ctx=None):
    """Guard. The driver site the step-04 golden tree still carries.

    Keyed to *this* tree, not to the pinned one. Rule 5 asks the untouched
    predecessor to fail the scored family completely, and against the pinned
    tree's seventeen sites it already scores 9/17 -- it would arrive holding
    more than half of a family. §24.4.

    Seven of its eight sites are in files `migrated:inventory` deletes
    outright, so they are recorded into that fixture's `drop` and leave this
    denominator: one deletion should not read as eight things done. What is
    left is `app/plugin/doctrine.php:47`, the fallback assignment, which is a
    site no deletion reaches.
    """
    return mysqlgone.evaluate(ROOTS, fixture=DRIVERS,
                              family="migrated:drivers")


def _inventory(ctx=None):
    return retire.inventory_family(ROOTS, fixture=INVENTORY)


def _server(ctx=None):
    """Guard. The two MySQL directories that travel as artifacts are gone.

    Two checks and no third one, so that the untouched step-04 tree scores it
    at exactly 0 and the guard product zeroes the whole reward. Rule 6 asks for
    nop 0.000, and this step is the first whose nop tree is a *finished* tree:
    the behaviour families it inherits pass on it for free, so something in the
    guard product has to bottom out or that free weight survives.
    """
    return retire.server_family()


def _postgres(ctx=None):
    """Guard. `/var/lib/postgresql` came back non-empty.

    Split out of `_server` (§24.9): a do-nothing run passes this, so it cannot
    be scored, and its meaning is the opposite of that family's anyway -- that
    one asks whether the retirement happened, this one asks whether it took the
    live cluster with it. It is also what stops a wholesale artifact-round-trip
    failure from reading as two clean retirements.
    """
    return retire.postgres_family()


def _wiring(ctx=None):
    """Scored. Five checks, and not one of them asks whether something is gone.

    §24.2 put the nineteen literals here on the argument that passing one
    requires editing a file the inventory keeps. §24.14 measured that argument
    and it is false: `sed -i -E '/mysql|mariadb/Id' <the four files>` edits
    them and passes all nineteen. An absence question has no mechanical floor,
    whatever file it is asked about.

    These five ask the opposite question -- does what is left still resolve --
    and the line filter fails every one of them, because deleting the line
    that names MySQL is exactly what leaves a fallback assigned from nothing,
    a switch arm that imports nothing, and a default keyword list naming a
    class the registry no longer has. §24.15 records the measured
    (handed, filtered, golden) signature of each.
    """
    return retire.wiring_earned(ROOTS, fixture=WIRING)


def _wiring_gone(ctx=None):
    """Guard. The nineteen literals that still select a MySQL back-end, in the
    four files the inventory keeps.

    It must still be true -- a tree that left `case "pdo_mysql":` in the
    harness has not retired MySQL -- and it earns nothing, because a line
    filter does it. §24.14.
    """
    return retire.wiring_gone(ROOTS, fixture=WIRING)


def _wiring_intact(ctx=None):
    """Guard. The same two passes' remainder: the tree still parses, no MySQL
    name has appeared in greater number than this codebase ever had, no class
    was left without the parent its file used to declare, and the two Doctrine
    registries still name classes the tree declares. A do-nothing run passes
    all eight, which is exactly why they earn nothing.

    `wiring/live/fallback-present` is here rather than in the scored family for
    that reason and no other. It asks only whether the Doctrine bootstrap still
    assigns a default adapter at all, and the handed tree assigns one, so the
    rule-6 floor run passed it -- 1/6 of 0.70 to a tree that had done nothing,
    which is the same defect §24.14 found in the nineteen literals, one round
    later and one check smaller. Deleting the default is still a failure; it is
    just not evidence of work. §24.16.
    """
    return retire.wiring_intact(ROOTS, fixture=WIRING)


def _provisioning(ctx=None):
    """Guard. `vagrant_bootstrap.sh` builds a PostgreSQL machine.

    Twelve checks in one family rather than the earned/intact split the wiring
    families use, because neither half survives rule 4.3's mechanical pass
    (§24.13) and a split whose halves are both guards buys nothing but a
    second multiplier.
    """
    return retire.provisioning_family(ROOTS[0])


FAMILIES = [
    # Off the disk, before anything has connected to anything.
    ("migrated:inventory", _inventory),
    ("migrated:drivers", _drivers),
    ("migrated:server-retired", _server),
    ("migrated:postgres-intact", _postgres),
    ("migrated:wiring-gone", _wiring_gone),
    ("migrated:wiring-intact", _wiring_intact),
    ("migrated:provisioning", _provisioning),
    # The five that read the delivered database as the authority, before the
    # corpus replay rewrites it.
    ("migrated:sequences", datagate.sequences_family),
    ("behavior:data-script", datagate.loader_family),
    ("migrated:install-data", datagate.install_data_family),
    ("migrated:schema", _widened(pgstep.schema_family)),
    ("behavior:schema-semantics", pgstep.semantics_family),
    # One corpus pass, four readings of it.
    ("behavior:requests", _corpus("behavior:requests", ("requests",))),
    ("behavior:tables-before",
     _corpus("behavior:tables-before", ("tables_before",))),
    ("behavior:tables-untouched",
     _corpus("behavior:tables-untouched", ("tables_after_unwritten",))),
    ("behavior:tables-after",
     _corpus("behavior:tables-after", ("tables_after_written",))),
    ("migrated:runtime-identity", _runtime("migrated:runtime-identity")),
    ("migrated:runtime-sql", _runtime("migrated:runtime-sql")),
    # The two that run the candidate's own PHP over the whole tree.
    ("migrated:wiring", _wiring),
    ("migrated:dialect", pgstep.dialect_family),
    # Last, because they are the only thing left a tree could still damage.
    ("migrated:install-file", _widened(pgstep.install_family)),
    ("migrated:install-schema", _widened(pgstep.install_schema_family)),
    ("behavior:install-semantics", _widened(pgstep.install_semantics_family)),
]

# Rule 5's two kinds: behaviour 0.30, migrated-ness 0.70.
#
# Both behaviour weights are inherited from step 04 in proportion -- 0.55/0.05
# there becomes 0.27/0.03 here -- because this step is not asking for a
# behavioural change and the behaviour families are here to make sure it does
# not cause one. They are free to the tree the candidate is handed. Rule 5
# requires them anyway, and the ~30% floor it sets is the reason the one
# scored `migrated:` family cannot simply take the whole step.
#
# `migrated:wiring` at 0.70 is the whole migrated side because it is the
# step's own sentence, and after §24.14 the sentence is sharper than "no
# MySQL name appears". Five checks, in the four files the inventory keeps:
#
#     the Doctrine fallback in `app/plugin/doctrine.php` still exists, still
#     assigns a back-end name, and names one whose driver class this tree
#     actually declares --
#
#         if ($adapter == "mysql" or $adapter == "mysqli" or !$adapter)
#             $adapter = "pdo_mysql";
#
#     clear the adapter setting and the handed tree still reaches for the old
#     server; delete the two lines and it reaches for nothing, which is not an
#     answer either;
#
#     the adapter harness still resolves -- every arm it kept imports a test
#     module that is a file in this tree, and the arm for the back-end that
#     survives still has its imports;
#
#     the reserved-words default list still names only back-ends the registry
#     above it still maps.
#
# Every one of them is false on the tree the step is handed, false under
# `sed -i -E '/mysql|mariadb/Id', and true on the golden tree. §24.15 has
# the three-tree measurement, per check.
#
# The behaviour side is 0.30 and not a point more. It is rule 5's floor, and
# in a step that asks for no behavioural change it is also the most any
# do-nothing-destructive pass can collect, so every 0.01 above the floor is
# 0.01 handed to the mechanical sweep rule 4.3 caps at ~30%.
#
# No weight here was chosen to clear a bar, and §24.14 did not move one. The
# bar was cleared by moving the free subset into the guards --
# `migrated:provisioning` (§24.13) and then the nineteen literals themselves
# (§24.14) -- which is a structural change rather than an arithmetic one.
#
# The arithmetic, unchanged across both cuts: 0.27 + 0.03 = 0.30 on the
# behaviour side, which is rule 5's ~30% floor and not a point more, and 0.70
# on the one scored `migrated:` family. A mechanical pass that breaks nothing
# collects the 0.30 and 0/5 of the 0.70, so its ceiling is 0.30 -- rule 4.3's
# ~30%, arrived at by what the families ask rather than by how they are
# weighted. §24.13 and §24.15 record what the sweeps then scored.
WEIGHTS = {
    "behavior:requests": 0.27,
    "behavior:tables-after": 0.03,
    "migrated:wiring": 0.70,
}

# Everything else. `behavior:mysql-intact` is deliberately absent: it is the
# one family from step 04 that this step contradicts.
GUARDS = ("migrated:inventory", "migrated:server-retired",
          "migrated:postgres-intact", "migrated:drivers",
          "migrated:wiring-gone", "migrated:wiring-intact",
          "migrated:provisioning",
          "behavior:tables-before", "behavior:tables-untouched",
          "migrated:sequences", "behavior:data-script", "migrated:install-data",
          "migrated:schema", "behavior:schema-semantics",
          "migrated:runtime-identity", "migrated:runtime-sql",
          "migrated:dialect",
          "migrated:install-file", "migrated:install-schema",
          "behavior:install-semantics")
