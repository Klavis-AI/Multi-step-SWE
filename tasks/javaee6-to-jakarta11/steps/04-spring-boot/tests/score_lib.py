"""Shared scoring library for every step of this task.

Per-step copy: the verifier image is built with that step's tests/ as its
Docker build context, so it cannot read anything above it. Every step's
tests/ holds an identical copy of this file; keep them in sync.

The score a step reports is

    reward = migration * behaviour

  migration   the weighted fraction of this step's migrated-ness gates that
              hold. Every gate names a construct class the instruction asked
              for, so an untouched tree scores 0 here by construction -- even
              at a step whose predecessor already built and already reproduced
              every recorded observation.

  behaviour   the fraction of the recorded observations the built artifact
              reproduces. A tree that does not build reproduces none of them,
              so it scores 0 without a separate rule for it.

Multiplying rather than averaging is deliberate. A candidate cannot buy a
passing score by deleting the behaviour the step was supposed to preserve, and
cannot buy one by preserving behaviour it never migrated.
"""

import json
import os
import re
import subprocess
import sys
import time

OUT = os.environ.get("SCORE_OUT", "/score/out")
LOG = os.environ.get("SCORE_LOG", "/score/log")


# ---------------------------------------------------------------------------
# journal
# ---------------------------------------------------------------------------

def _j(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path


def record(key, value):
    """Append one machine-readable fact to the scorer's journal.

    Everything a human would want to re-derive later goes here, not only into
    prose: the counts a gate compared, the names it found, the exit status of
    every build. The same file is written by helpers.sh's `record`.
    """
    with open(_j(os.path.join(OUT, "journal.tsv")), "a", encoding="utf-8") as fh:
        fh.write("%s\t%s\n" % (key, value))


def say(*words):
    sys.stderr.write("[%s] %s\n"
                     % (time.strftime("%H:%M:%S", time.gmtime()),
                        " ".join(str(w) for w in words)))
    sys.stderr.flush()


# ---------------------------------------------------------------------------
# running things
# ---------------------------------------------------------------------------

UID = 65534
GID = 65534


def run(argv, cwd=None, env=None, timeout=1800, log=None, unprivileged=True,
        stdin=None):
    """Run a command, optionally as nobody, and tee its output to a log file.

    Candidate code runs unprivileged for the reason harden_reward_dir exists:
    Harbor opens /logs/verifier to 0777 just before this container starts, and
    a build that runs as root can write the reward file directly. That includes
    anything a Makefile, a plugin, or a static initializer starts.
    """
    if unprivileged:
        argv = ["setpriv", "--reuid", str(UID), "--regid", str(GID),
                "--clear-groups", "env"] + \
               ["%s=%s" % (k, v) for k, v in sorted((env or {}).items())] + \
               list(argv)
        env = None
    else:
        e = dict(os.environ)
        e.update(env or {})
        env = e
    t0 = time.time()
    try:
        p = subprocess.run(argv, cwd=cwd, env=env,
                           stdin=subprocess.DEVNULL if stdin is None
                           else subprocess.PIPE,
                           input=stdin,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        rc, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        rc, out = "TIMEOUT", (e.stdout or b"")
    except FileNotFoundError as e:
        rc, out = "ENOENT", str(e).encode()
    dt = time.time() - t0
    if log:
        with open(_j(os.path.join(LOG, log)), "wb") as fh:
            fh.write(out)
    record("ran", "%s rc=%s %.1fs %s"
           % (log or argv[0], rc, dt, " ".join(argv[-4:])))
    return rc, out.decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# scans
# ---------------------------------------------------------------------------

class Scan(object):
    """One JSON-Lines scan, indexed by record kind.

    Both scanners emit one object per line and never merge records, so a gate
    can always say which file, class or line it is talking about. Reading the
    whole file into memory is deliberate: the largest scan this task produces
    is a few megabytes, and a streaming reader would make every gate a loop.
    """

    def __init__(self, path):
        self.path = path
        self.by_kind = {}
        self.bad = 0
        if not os.path.exists(path):
            return
        with open(path, "r", encoding="utf-8") as fh:
            for ln in fh:
                ln = ln.strip()
                if not ln:
                    continue
                try:
                    o = json.loads(ln)
                except ValueError:
                    self.bad += 1
                    continue
                self.by_kind.setdefault(o.get("kind", "?"), []).append(o)

    def kind(self, name):
        return self.by_kind.get(name, [])

    def __len__(self):
        return sum(len(v) for v in self.by_kind.values())


class Artifact(Scan):
    """A bytecode scan: what the built artifact actually references."""

    def classes(self):
        return self.kind("class")

    def entries(self):
        return self.kind("entry")

    def refs(self):
        """Every type name any class in the artifact refers to, however.

        Symbolic references, descriptors, signatures, annotations, constant
        pool entries and dotted string literals all land here, so a dependency
        reached by Class.forName is as visible as one reached by a field
        access. A gate that asks "is javax.servlet gone" asks this.
        """
        out = set()
        for c in self.classes():
            out.update(c.get("r") or [])
            out.update(c.get("l") or [])
            out.update(c.get("a") or [])
            out.update(c.get("i") or [])
            if c.get("s"):
                out.add(c["s"])
        return out

    def refs_by_class(self, prefixes, own=False):
        """Which classes refer to a name under any of these prefixes.

        Returns {class name: sorted list of matching references}. A gate
        reports this rather than a bare count so that a failure names the file
        to open, and so that the same call proves both directions: the old
        prefix is empty and the new one is not.

        `own` restricts the question to the classes this project compiled, by
        the same origin test class_versions uses. Which way round to ask is a
        per-gate decision and not a default:

          * "is javax.servlet gone" is about the whole deployable, dependency
            jars included -- a war whose WEB-INF/lib still ships a servlet-2.5
            JGit has not migrated, however clean its own bytecode is. own=False.
          * "does this tree use java.nio.file" is about the candidate's code
            only. A dependency that happens to reference the package would
            otherwise pass the gate for a tree that never touched it. own=True.
        """
        hits = {}
        for c in self.classes():
            if own and "!" in (c.get("o") or ""):
                continue
            names = set(c.get("r") or []) | set(c.get("l") or []) \
                | set(c.get("a") or []) | set(c.get("i") or [])
            if c.get("s"):
                names.add(c["s"])
            m = sorted(n for n in names if _under(n, prefixes))
            if m:
                hits[c.get("n", "?")] = m
        return hits

    def own_classes(self, origin_contains=None):
        """The class records this project compiled, optionally by origin.

        The universe an `own=True` gate searches. An absence gate that reports
        "0 sites" over an empty universe has measured nothing and says so in
        exactly the words it uses when the tree is clean, which is why every
        such gate is asked for this number as well. `origin_contains` narrows
        it to one output directory -- `test-classes/` is the one that goes
        empty when a candidate deletes the suite rather than porting it.
        Origins are relative to the root that was scanned and so carry no
        leading slash; a substring written with one matches nothing, and the
        gate then reports a perfectly clean tree as vacuous.
        """
        out = []
        for c in self.classes():
            if "!" in (c.get("o") or ""):
                continue
            if origin_contains and origin_contains not in (c.get("o") or ""):
                continue
            out.append(c)
        return out

    def class_versions(self):
        """{class name: class-file major version}, own classes only.

        "Own" means the ones this project compiled: a class that came out of a
        dependency jar inside the artifact is not evidence about the release
        the project targets. Origins containing "!" came from an archive.
        """
        return {c["n"]: c["v"] for c in self.classes()
                if "!" not in (c.get("o") or "")}

    def all_class_versions(self):
        return {(c.get("o") or "") + "#" + c["n"]: c["v"] for c in self.classes()}


class Ast(Scan):
    """A source scan: what the repository's Java actually says."""

    def files(self):
        return self.kind("file")

    def parse_errors(self):
        return self.kind("parse-error")

    def nodes(self, type_=None):
        ns = self.kind("node")
        return [n for n in ns if type_ is None or n.get("t") == type_]

    def suppressions(self):
        return self.kind("suppress")

    def typeuses(self, prefixes=None):
        us = self.kind("typeuse")
        if prefixes is None:
            return us
        return [u for u in us if _under(u.get("n", ""), prefixes)]


def _under(name, prefixes):
    for p in prefixes:
        if name == p or name.startswith(p + ".") or name.startswith(p + "$"):
            return True
    return False


def under(name, prefixes):
    return _under(name, prefixes)


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------

class Result(object):
    def __init__(self, name, ok, detail, weight, evidence=None):
        self.name = name
        self.ok = bool(ok)
        self.detail = detail
        self.weight = float(weight)
        self.evidence = evidence or {}

    def as_dict(self):
        d = {"name": self.name, "ok": self.ok, "detail": self.detail,
             "weight": self.weight}
        if self.evidence:
            d["evidence"] = self.evidence
        return d


class Gates(object):
    """The migrated-ness half of the score.

    Every gate is one construct class the instruction named, weighted by how
    much of the step it is. A gate that cannot be evaluated -- because the
    scan it reads is missing -- counts as failed and says so; silently
    skipping it would turn a broken scanner into a free pass.
    """

    def __init__(self):
        self.results = []

    def add(self, name, ok, detail, weight=1.0, evidence=None):
        r = Result(name, ok, detail, weight, evidence)
        self.results.append(r)
        record("gate", "%s\t%s\t%.3f\t%s"
               % (name, "pass" if r.ok else "FAIL", r.weight, detail))
        say("gate %-38s %-4s w=%.2f  %s"
            % (name, "ok" if r.ok else "FAIL", r.weight, detail))
        return r

    def absent(self, name, hits, weight=1.0, limit=12):
        """A construct class the step named for removal is gone.

        `hits` is a mapping or a sequence of sites. The detail always carries
        the count, and a failure carries the first few names, because a gate
        whose failure says only "false" cannot be acted on.
        """
        items = sorted(hits.keys()) if isinstance(hits, dict) else sorted(hits)
        ok = not items
        detail = "%d site(s)" % len(items)
        if items:
            detail += ": " + ", ".join(str(i) for i in items[:limit])
            if len(items) > limit:
                detail += ", +%d more" % (len(items) - limit)
        return self.add(name, ok, detail, weight,
                        {"count": len(items), "sites": [str(i) for i in items[:200]]})

    def present(self, name, hits, minimum=1, weight=1.0, limit=8):
        """The replacement the step named is actually there, in numbers.

        A migration is not done when the old name is gone; it is done when the
        new one is in use. Asserting the count separately is what keeps
        "deleted the whole module" from passing the removal gate.
        """
        items = sorted(hits.keys()) if isinstance(hits, dict) else sorted(hits)
        ok = len(items) >= minimum
        detail = "%d site(s), need >=%d" % (len(items), minimum)
        if items:
            detail += ": " + ", ".join(str(i) for i in items[:limit])
        return self.add(name, ok, detail, weight,
                        {"count": len(items), "need": minimum,
                         "sites": [str(i) for i in items[:200]]})

    def score(self):
        tw = sum(r.weight for r in self.results)
        if tw <= 0:
            return 0.0
        got = sum(r.weight for r in self.results if r.ok)
        return got / tw

    def report(self):
        return [r.as_dict() for r in self.results]


# ---------------------------------------------------------------------------
# the step's final answer
# ---------------------------------------------------------------------------

def finish(gates, behaviour, extra=None):
    """Write the step report and return the reward.

    behaviour is (passed, total). A total of zero means the suite never ran --
    the build failed, or the server never came up -- and that is a zero, not a
    division by zero and not a free 1.0: a mean over an empty set is not a
    score.
    """
    passed, total = behaviour
    b = (float(passed) / total) if total else 0.0
    g = gates.score()
    reward = g * b
    rep = {"migration": round(g, 6),
           "behaviour": round(b, 6),
           "behaviour_passed": passed,
           "behaviour_total": total,
           "reward": round(reward, 6),
           "gates": gates.report()}
    if extra:
        rep.update(extra)
    with open(_j(os.path.join(OUT, "report.json")), "w", encoding="utf-8") as fh:
        json.dump(rep, fh, indent=1, sort_keys=True)
    record("migration", "%.6f" % g)
    record("behaviour", "%d/%d" % (passed, total))
    record("reward", "%.6f" % reward)
    say("migration=%.4f behaviour=%d/%d reward=%.4f"
        % (g, passed, total, reward))
    return reward


# ---------------------------------------------------------------------------
# small shared predicates
# ---------------------------------------------------------------------------

def xml_texts(path):
    """Every text node and attribute value in an XML file, as a flat list.

    Used by the gates that ask what a pom or a web.xml declares. Parsing
    rather than grepping, because a commented-out dependency is not a
    dependency, and a grep cannot tell.
    """
    import xml.etree.ElementTree as ET
    out = []
    try:
        tree = ET.parse(path)
    except Exception as e:                       # noqa: BLE001 - reported
        return ["!unparsable: %s" % e]
    for el in tree.iter():
        if el.text and el.text.strip():
            out.append(el.text.strip())
        for v in el.attrib.values():
            if v.strip():
                out.append(v.strip())
    return out


def find_files(root, names=None, suffixes=None, skip_dirs=("target", ".git")):
    """Walk a tree for files by name or suffix.

    Everything is scanned and nothing excluded, so the only directories
    skipped here are build output and VCS metadata -- neither is source, and
    target/ holds a copy of the source that would double every count.
    """
    hits = []
    for base, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in skip_dirs)
        for f in sorted(files):
            if names and f in names:
                hits.append(os.path.join(base, f))
            elif suffixes and f.endswith(tuple(suffixes)):
                hits.append(os.path.join(base, f))
    return hits


MAJOR = {45: "1.1", 46: "1.2", 47: "1.3", 48: "1.4", 49: "5", 50: "6",
         51: "7", 52: "8", 53: "9", 54: "10", 55: "11", 56: "12", 57: "13",
         58: "14", 59: "15", 60: "16", 61: "17", 62: "18", 63: "19", 64: "20",
         65: "21", 66: "22", 67: "23", 68: "24", 69: "25"}


def release_of(major):
    return MAJOR.get(major, "major-%d" % major)
