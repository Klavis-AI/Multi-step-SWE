"""Turn the observations into the migrated-ness family's score.

The shape of the scoring is the point, so it is written down here rather than
spread through eight verifiers.

Every step's manifest splits its checks in two:

**new**      checks the tree the agent was *handed* fails. These are the step's
             actual work. Credit is 1.0 only at zero; a tree that removed some
             of the construct but not all gets at most 0.5, scaled by how much
             it removed. Full credit for a partial job would contradict the
             "at zero" the task line asks for; zero credit for a nine-tenths
             job would leave the trajectory analysis with no gradient to read.

**carried**  checks the handed-over tree already passes -- every construct an
             earlier step retired. Credit is binary, and so is the family: a
             regression is a regression, half a regression is not a thing, and
             one regression out of six is not five-sixths of a submission.

The two combine multiplicatively::

    gate_score = mean(new credits) * all(carried credits)

so the untouched previous-step tree scores 0 (it fails every new check by
construction, which is how the manifest was built), a tree that does the step's
work and quietly reintroduces something from step 2 loses the whole family
rather than a rounding error, and only a tree that does the new work without
regressing scores 1.

One precondition sits in front of both means. Every check here counts
*occurrences of an old construct*, and lower is better, so a tree with less
code in it scores better for free: replace each module with an empty file and
no old construct survives anywhere. That is not a migration, and
``importable()`` is what says so -- if the package the tree installs cannot be
imported, or exposes no public names, the family is 0 and the report says which
of the two it was. The behavior family would have caught the same tree, and
under a multiplicative reward (see ``grade``) its zero already ends the matter;
this check is here so that the number has the right reason attached to it,
which is what someone reading the report a month from now needs.
"""
from __future__ import print_function, absolute_import, division


class Check(object):
    """One gate. ``observed`` and ``entry`` are counts; lower is better."""

    def __init__(self, spec, observed, entry):
        self.name = spec["name"]
        self.kind = spec["kind"]
        self.spec = spec
        self.observed = observed
        self.entry = entry
        self.limit = spec.get("limit", 0)

    def credit(self, graded):
        if self.observed <= self.limit:
            return 1.0
        if not graded:
            return 0.0
        room = self.entry - self.limit
        if room <= 0:
            return 0.0
        removed = float(self.entry - self.observed) / room
        if removed <= 0:
            return 0.0
        return 0.5 * min(1.0, removed)

    def as_dict(self, graded):
        return {"name": self.name, "kind": self.kind,
                "observed": self.observed, "entry": self.entry,
                "limit": self.limit, "graded": graded,
                "credit": round(self.credit(graded), 6),
                "detail": self.spec.get("detail", "")}


def observe(spec, obs):
    """Read one check's count out of the observation bundle."""
    kind = spec["kind"]
    if kind == "ast":
        return obs["ast_counts"].get(spec["rule"], 0)
    if kind == "ast-sum":
        # Several rules that are one property, counted together. `new_mean` is
        # a flat mean over checks, so a two-line construct listed as its own
        # check would weigh as much as an eighteen-site one; grouping by the
        # property the step is actually asking for keeps the gradient honest.
        # The individual rule counts are still in the observation bundle, so
        # nothing is hidden by the grouping.
        return sum(obs["ast_counts"].get(rule, 0) for rule in spec["rules"])
    if kind == "ruff":
        code = spec["code"]
        total = 0
        for found, n in obs["ruff"].get("by_code", {}).items():
            if found == code or found.startswith(code):
                total += n
        if obs["ruff"].get("error"):
            # ruff failing to run is not a clean tree. Report a count that
            # cannot pass instead of a zero that would.
            return max(1, total)
        return total
    if kind == "suppressions":
        return obs["suppressions"]["total"]
    if kind == "warnings":
        cats = spec.get("categories")
        census = obs["warnings"]["by_category"]
        if not cats:
            return sum(census.values())
        return sum(census.get(c, 0) for c in cats)
    if kind == "absent-modules":
        return len(obs["absent_modules_present"])
    if kind == "versions":
        # One count per pin that does not match. The expected table lives in
        # the manifest so a step's pins are visible in one place.
        bad = 0
        for name, want in sorted(spec["expect"].items()):
            got = obs["versions"].get(name)
            if want is None:
                if got is not None:
                    bad += 1
            elif got is None or not str(got).startswith(str(want)):
                bad += 1
        return bad
    if kind == "semantics":
        bad = 0
        for key, want in sorted(spec["expect"].items()):
            got = obs["semantics"].get(key)
            if _norm(got) != _norm(want):
                bad += 1
        return bad
    if kind == "paths":
        # How many of the files this step was handed are no longer in the tree.
        # Carried, and therefore binary: the instruction says preserve the
        # supplied source, and a tree that answers a parse gate by deleting the
        # file that would not parse has not done the step.
        have = set(obs.get("tree_paths") or [])
        return len([p for p in spec["expect"] if p not in have])
    if kind == "names":
        # How many of the *definitions* the handed-over tree made are no longer
        # defined anywhere in the file. `paths` says the file is still there;
        # this says the file still holds its defs and classes and their methods,
        # which is the shape a gutting removes and a migration keeps. Scored
        # against `def_names`, not the fuller `names`, so retiring a dead import
        # or a collapsed version flag is not read as gutting (see census.py).
        # `allow` lists definitions a step is meant to remove, so a deliberate
        # drop is written down in the manifest rather than silently tolerated,
        # exactly as in `public-api`.
        seen = obs.get("census") or {}
        allow = set(spec.get("allow", []))
        missing = 0
        for path, names in sorted(spec["expect"].items()):
            have = set((seen.get(path) or {}).get("def_names") or [])
            for name in names:
                if name in have or path + "::" + name in allow:
                    continue
                missing += 1
        return missing
    if kind == "asserts":
        # How many test modules make fewer assertions than they were handed.
        # The inherited-suite table in `suite` asks that every recorded node id
        # still collects and still passes, and a body replaced by `pass` does
        # both; this is the number that tells the two apart. A floor, not an
        # equality, because adding a test is not a regression.
        seen = obs.get("census") or {}
        weakened = 0
        for path, floor in sorted(spec["expect"].items()):
            got = (seen.get(path) or {}).get("asserts")
            if got is None or got < floor:
                weakened += 1
        return weakened
    if kind == "substance":
        # How many files hold less code than the floor the manifest records.
        # `names` says every definition is still bound; this says the
        # definitions still have bodies, which `names` cannot -- giving each of
        # a class's methods a body of `pass` binds exactly the names it bound
        # before. Counted in non-blank non-comment lines, which do not change
        # unit when a file starts parsing partway through the migration.
        #
        # The floors are set well under the handed-over size rather than at it.
        # Migrating this tree is almost entirely substitution, so the reference
        # solution leaves every file's line count unchanged but one, which it
        # takes to 0.80 of entry by dropping a nose-era fixture; an agent that
        # tidies harder than the reference should not be failed for it. The
        # cheapest gutting attack, by contrast, lands near 0.05 -- there is an
        # order of magnitude between the two, and the floor sits in it.
        seen = obs.get("census") or {}
        gutted = 0
        for path, floor in sorted(spec["expect"].items()):
            got = (seen.get(path) or {}).get("lines")
            if got is None or got < floor:
                gutted += 1
        return gutted
    if kind == "retention":
        # How many files hold less of the source they were *handed* than the
        # floor. `substance` counts lines, and a count is something a file can
        # buy back: gut every body, append two hundred `_pad_n = n`, and the
        # line count clears the floor with the code still gone -- measured at
        # 1.000000 before this row existed. This counts the handed-over lines
        # that are still present, which padding cannot buy, because the only
        # way to raise it is to put the original lines back.
        #
        # Set-valued and stripped, so re-indenting is free and a repeated line
        # is not worth more than a single one. The floor is the same 0.60 as
        # `substance` and sits in the same gap: the reference solution's worst
        # file keeps 0.793 of its lines, and the padding attack keeps 0.133.
        # What this does *not* tolerate is a wholesale reformat of a file --
        # re-quoting every string would drop retention without removing work.
        # The instruction asks for the supplied source to be preserved, and
        # every other source-shape row here takes the same position.
        seen = obs.get("census") or {}
        floor = spec.get("floor", 0.6)
        width = spec.get("width", 6)
        thin = 0
        for path, packed in sorted(spec["expect"].items()):
            want = set(packed[i:i + width]
                       for i in range(0, len(packed), width))
            if not want:
                continue
            got = set((seen.get(path) or {}).get("digests") or [])
            if len(want & got) < floor * len(want):
                thin += 1
        return thin
    if kind == "unparsable":
        return obs["ast_counts"].get("unparsable", 0)
    if kind == "public-api":
        # How many names the step was supposed to keep are missing. `allow`
        # lists the ones a step is permitted to drop, so a deliberate removal
        # is written down in the manifest instead of silently tolerated.
        api = obs.get("repro", {}).get("public_api", {})
        allow = set(spec.get("allow", []))
        missing = 0
        for module, names in sorted(spec["expect"].items()):
            got = api.get(module) or {}
            if "names" not in got:
                # The module did not import at all: every name it owed is gone.
                missing += len([n for n in names
                                if module + "." + n not in allow])
                continue
            have = set(got["names"])
            for name in names:
                if name in have or module + "." + name in allow:
                    continue
                missing += 1
        return missing
    if kind == "self-warnings":
        # Compile-time and import-time warnings the tree raises about its own
        # files. `categories` narrows it to the ones a step is responsible for.
        warn = obs.get("repro", {}).get("self_warnings", {})
        cats = spec.get("categories")
        if not cats:
            return warn.get("total", 9999)
        census = warn.get("by_category", {})
        return sum(census.get(c, 0) for c in cats)
    if kind == "repro":
        # One count per named reproducibility check that did not come back the
        # way the manifest says it must. A check the observation never produced
        # counts as failed, so a repro probe that crashed cannot read as clean.
        checks = obs.get("repro", {}).get("checks", {})
        bad = 0
        for name, want in sorted(spec["expect"].items()):
            if bool(checks.get(name)) != bool(want):
                bad += 1
        return bad
    raise KeyError("unknown check kind: %s" % kind)


def importable(obs):
    """Is the tree these counts were taken on a package at all?

    The signal is the public-API probe's entry for the root module: it holds
    ``names`` when ``import skutil`` worked in a fresh interpreter, and
    ``error`` when it did not. Returns ``(ok, detail)``.

    Three states, not two. ``observe --no-repro`` -- what the authoring aids
    use to get counts without spawning interpreters -- produces a bundle with
    no public-API answer in it at all, and a tree nobody looked at is not a
    tree that failed. That case answers "yes" and records that it was not
    checked, so the distinction survives into the report instead of being
    flattened into a pass. Every bundle a verifier produces has the answer,
    because ``cmd_observe`` runs the probe unless it is told not to.

    A probe that crashed counts as a failure, the same way a ruff that would
    not start counts as findings and a warnings probe that died counts as 9999
    warnings: the alternative is that breaking the instrument reads as a clean
    result.
    """
    repro = obs.get("repro") or {}
    if "public_api" not in repro:
        return True, {"checked": False,
                      "why": "no public-API probe in this observation"}
    api = repro.get("public_api") or {}
    root = api.get("skutil")
    if not isinstance(root, dict) or "names" not in root:
        why = root.get("error") if isinstance(root, dict) else None
        return False, {"checked": True,
                       "why": str(why or api.get("error")
                                 or "skutil did not import")[:200]}
    # The root package itself legitimately exports nothing but submodules, and
    # the probe drops submodules, so ``skutil`` alone is expected to come back
    # with an empty list. What has to be non-empty is the package taken
    # together.
    total = 0
    for got in api.values():
        if isinstance(got, dict) and isinstance(got.get("names"), list):
            total += len(got["names"])
    if total:
        return True, {"checked": True, "public_names": total}
    return False, {"checked": True,
                   "why": "skutil imported but no module exposed a public name"}


def _norm(value):
    if isinstance(value, (list, tuple)):
        return [str(v) for v in value]
    return str(value)


def score(manifest, obs):
    """The family score plus the per-check detail the report carries."""
    groups = {}
    for group in ("new", "carried"):
        graded = group == "new"
        rows = []
        for spec in manifest.get(group, []):
            check = Check(spec, observe(spec, obs), spec.get("entry", 0))
            rows.append(check.as_dict(graded))
        groups[group] = rows

    def mean(rows):
        if not rows:
            return 1.0
        return sum(r["credit"] for r in rows) / float(len(rows))

    def all_or_nothing(rows):
        """Carried credit, over the family rather than per row.

        The docstring above says a tree that reintroduces something from an
        earlier step "loses the whole family rather than a rounding error". A
        flat mean does not do that: it charges one regression 1/n, which gets
        *cheaper* every time a later audit adds a carried row -- so adding a
        check would raise the score of a tree that regresses. Binary per row
        and binary over the rows is the shape the paragraph describes, and it
        is the only one under which adding a check cannot reward anybody.
        """
        return 1.0 if all(r["credit"] >= 1.0 for r in rows) else 0.0

    new_mean = mean(groups["new"])
    carried_mean = all_or_nothing(groups["carried"])
    ok, import_detail = importable(obs)
    return {
        "step": manifest.get("step"),
        "new": groups["new"],
        "carried": groups["carried"],
        "new_mean": round(new_mean, 6),
        "carried_mean": round(carried_mean, 6),
        "importable": ok,
        "import_detail": import_detail,
        "score": round(new_mean * carried_mean, 6) if ok else 0.0,
    }
