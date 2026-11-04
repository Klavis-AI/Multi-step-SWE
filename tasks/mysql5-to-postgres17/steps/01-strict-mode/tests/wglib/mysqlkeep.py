"""MySQL stays where it is: the old deployment's artifact, read as bytes.

Step 04 moves the application off MySQL and says, in as many words, that MySQL
itself does not move: *"`/var/lib/mysql` and `/etc/mysql` are handed on whole:
the data directory with its system tablespace and its redo logs, the `mysql`
system schema, the `webgoatphp` database and every table in it, and the
configuration that still sets the strict SQL mode the first milestone put
there. [...] Nothing here removes it, empties it, or takes it apart."*
Nothing in the step scored that, and a stated requirement nothing scores is
worth less than no requirement -- it teaches a reader that the prose is
decoration. The cheapest way through this step for a candidate short of disk
is `rm -rf /var/lib/mysql`, and until this family existed that cost nothing at
all.

This reads the data directory rather than asking a server, because the step-04
verifier image deliberately has no `mysqld` and no client (see
`common/tests/Dockerfile.pg`): the whole point of that image is that a
candidate cannot be carried by a MySQL server that happens to still be
running. So the requirement is checked the only way it can be checked here --
the artifact travels, and the artifact is looked at. That is weaker than
starting the server and querying it, and it is not nothing: an `.ibd` that is
gone is gone, and a `my.cnf` that has stopped setting a strict mode has
stopped setting one whether or not a server is up to read it.

Two things follow from reading the artifact instead of a server, and both are
deliberate.

Contents are not compared. The `[[verifier.collect]]` hook stops both servers
before the copy, so the directory is a clean-shutdown copy rather than a torn
one -- but a candidate who drove the application against MySQL while testing
(which the step invites: three milestones of it ran there) has written pages,
statistics rows and four dropped-and-rebuilt lesson tables into those files.
Their bytes are theirs to change. What must be there is the table, so each
table is checked for its files, present and non-empty, and not for a digest.

Extra tables are not penalised. The denominator is the recorded baseline, so
this asks whether what was there is still there, which is what the instruction
asks. A candidate who leaves a scratch table behind in MySQL has not taken
anything away.

This is a *guard* family (`gate.combine`): every check in it is true on the
tree the candidate is handed, so passing it is not evidence of work and it
must not pay. It enters as a cap.
"""

import json
import os

from . import gate
from . import strictgate

DATADIR = "/var/lib/mysql"
ETCDIR = "/etc/mysql"
MY_CNF = strictgate.MY_CNF
DATABASE = strictgate.DATABASE

FIXTURE = "/tests/fixtures/mysql-intact-04.json"

MAX_SHOWN = 8

# The checks that exist whatever the tree looks like. The per-table ones are
# named by the baseline, which is also fixed before the tree is read.
FIXED_IDS = ("keep/datadir",
             "keep/system-tablespace",
             "keep/redo-log",
             "keep/system-schema",
             "keep/app-database",
             "keep/etc",
             "keep/my-cnf",
             "keep/strict-mode")


# ------------------------------------------------------------------- read ---

def _files(path):
    """{basename: size} for the regular files directly in `path`.

    An unreadable directory reads as absent, not as empty: the caller fails
    every check underneath it either way, and the two are the same fact about
    this deployment.
    """
    out = {}
    try:
        names = os.listdir(path)
    except OSError:
        return None
    for name in sorted(names):
        full = os.path.join(path, name)
        try:
            st = os.stat(full)
        except OSError:
            continue
        if os.path.isfile(full):
            out[name] = st.st_size
    return out


def _tables(files):
    """{table name: [its file basenames]} from one database directory.

    MySQL 5.7 with file-per-table keeps `<table>.frm` beside `<table>.ibd`, and
    `db.opt` for the database's own defaults. The `.frm` is what makes the
    table exist to the server, so it is what names a table here; a stray `.ibd`
    with no `.frm` is not a table.
    """
    out = {}
    for name in sorted(files or {}):
        stem, ext = os.path.splitext(name)
        if ext == ".frm":
            out.setdefault(stem, [])
    for name in sorted(files or {}):
        stem, ext = os.path.splitext(name)
        if stem in out and ext in (".frm", ".ibd"):
            out[stem].append(name)
    return out


def observe(datadir=DATADIR, etcdir=ETCDIR, database=DATABASE,
            config=MY_CNF):
    """Everything this family scores, read once."""
    sysdir = os.path.join(datadir, "mysql")
    appdir = os.path.join(datadir, database)
    top = _files(datadir)
    sys_files = _files(sysdir)
    app_files = _files(appdir)
    mode, lines = strictgate.config_sql_mode(config)
    return {"datadir": datadir,
            "datadir_is_dir": os.path.isdir(datadir),
            "top": top,
            "system_dir": os.path.isdir(sysdir),
            "system_files": sys_files,
            "app_dir": os.path.isdir(appdir),
            "app_files": app_files,
            "tables": _tables(app_files),
            "etc_is_dir": os.path.isdir(etcdir),
            "my_cnf": (os.path.getsize(config)
                       if os.path.isfile(config) else None),
            "config_sql_mode": mode,
            "config_lines": list(lines)}


# ---------------------------------------------------------------- fixture ---

def load(path=FIXTURE):
    with open(path, "rb") as fh:
        return json.loads(fh.read().decode("utf-8"))


def record(datadir=DATADIR, etcdir=ETCDIR, database=DATABASE, config=MY_CNF):
    """AUTHORING: the baseline, recorded from the step-03 handoff tree.

    Sizes are recorded for the reader's benefit and are not scored -- see the
    header on why contents are not compared. What is scored is the set of
    names: the redo logs, the system schema's files, the database's tables and
    each table's files.
    """
    obs = observe(datadir, etcdir, database, config)
    if not obs["datadir_is_dir"]:
        raise RuntimeError("%s is not a directory" % datadir)
    if not obs["app_dir"]:
        raise RuntimeError("no %s database at %s"
                           % (database, os.path.join(datadir, database)))
    redo = sorted(n for n in obs["top"] if n.startswith("ib_logfile"))
    if not redo:
        raise RuntimeError("no ib_logfile* in %s" % datadir)
    return {"database": database,
            "system_tablespace": "ibdata1",
            # Recorded for the human reading a failure -- a tablespace that
            # shrank says something an inventory of names cannot -- and
            # deliberately not scored: InnoDB grows this file on its own
            # schedule, so any threshold over it would fail trees that did
            # nothing wrong.
            "system_tablespace_bytes": obs["top"].get("ibdata1"),
            "redo_logs": redo,
            "system_schema_files": sorted(obs["system_files"] or {}),
            "app_files": sorted(obs["app_files"] or {}),
            "tables": {t: sorted(f) for t, f in obs["tables"].items()},
            "strict": bool(strictgate.is_strict(obs["config_sql_mode"])),
            "config_sql_mode": obs["config_sql_mode"]}


# ---------------------------------------------------------------- scoring ---

def table_ids(fixture):
    return tuple("keep/table/%s" % t for t in sorted(fixture["tables"]))


def check_ids(fixture):
    return FIXED_IDS + table_ids(fixture)


def _missing(names, have):
    return [n for n in names if n not in (have or {})]


def _empty(names, have):
    return [n for n in names if (have or {}).get(n) == 0]


def _shown(names):
    if len(names) <= MAX_SHOWN:
        return ", ".join(names)
    return "%s and %d more" % (", ".join(names[:MAX_SHOWN]),
                               len(names) - MAX_SHOWN)


def evaluate(fixture, obs=None, family="behavior:mysql-intact"):
    """The guard: one check per thing the instruction says stays."""
    c = gate.Checks(family)
    if obs is None:
        obs = observe(database=fixture.get("database", DATABASE))
    ids = check_ids(fixture)

    if not obs["datadir_is_dir"]:
        # Everything under the data directory is unevaluable, which gate.py
        # reads as failed. The three checks outside it are still asked: a
        # deployment can lose one and keep the other.
        under = [i for i in ids
                 if i not in ("keep/etc", "keep/my-cnf", "keep/strict-mode")]
        c.fail_all(under, "%s is not a directory" % obs["datadir"])
    else:
        c.add("keep/datadir", True, "%s: %d file(s) at the top level"
              % (obs["datadir"], len(obs["top"] or {})))

        tsname = fixture["system_tablespace"]
        size = (obs["top"] or {}).get(tsname)
        c.add("keep/system-tablespace", bool(size),
              "%s: %s" % (tsname,
                          "%d bytes" % size if size
                          else ("empty" if size == 0 else "missing")))

        redo = fixture["redo_logs"]
        gone = _missing(redo, obs["top"])
        flat = _empty(redo, obs["top"])
        c.add("keep/redo-log", not gone and not flat,
              "%d redo log(s)%s%s"
              % (len(redo),
                 "; missing: " + _shown(gone) if gone else "",
                 "; empty: " + _shown(flat) if flat else ""))

        want = fixture["system_schema_files"]
        if not obs["system_dir"]:
            c.add("keep/system-schema", False,
                  "%s/mysql is not a directory" % obs["datadir"])
        else:
            gone = _missing(want, obs["system_files"])
            c.add("keep/system-schema", not gone,
                  "%d of %d recorded file(s) present%s"
                  % (len(want) - len(gone), len(want),
                     "; missing: " + _shown(gone) if gone else ""))

        db = fixture.get("database", DATABASE)
        tabs = table_ids(fixture)
        if not obs["app_dir"]:
            c.add("keep/app-database", False,
                  "no %s directory under %s" % (db, obs["datadir"]))
            c.fail_all(tabs, "the %s database directory is gone" % db)
        else:
            opt = (obs["app_files"] or {}).get("db.opt")
            c.add("keep/app-database", opt is not None,
                  "%s: %d file(s)%s"
                  % (db, len(obs["app_files"] or {}),
                     "" if opt is not None else "; db.opt missing"))
            for t in sorted(fixture["tables"]):
                want = fixture["tables"][t]
                gone = _missing(want, obs["app_files"])
                flat = _empty(want, obs["app_files"])
                c.add("keep/table/%s" % t, not gone and not flat,
                      "%s: %s" % (t,
                                  "ok (%s)" % ", ".join(want)
                                  if not gone and not flat
                                  else ("missing " + _shown(gone)
                                        if gone else "")
                                       + ("; " if gone and flat else "")
                                       + ("empty " + _shown(flat)
                                          if flat else "")))

    c.add("keep/etc", obs["etc_is_dir"],
          "%s: %s" % (ETCDIR,
                      "present" if obs["etc_is_dir"] else "not a directory"))
    c.add("keep/my-cnf", bool(obs["my_cnf"]),
          "%s: %s" % (MY_CNF,
                      "%d bytes" % obs["my_cnf"] if obs["my_cnf"]
                      else "missing or empty"))
    # Step 01's deliverable, restated by step 04's *What must not change*: "The
    # MySQL configuration that rejects rather than repairs". `is_strict` is
    # strictgate's own reading of it, so the two steps cannot disagree about
    # what counts -- STRICT_ALL_TABLES and TRADITIONAL pass here for the same
    # reason they passed there.
    mode = obs["config_sql_mode"]
    c.add("keep/strict-mode", strictgate.is_strict(mode),
          "sql_mode = %r%s" % (mode,
                               "" if obs["config_lines"]
                               else " (nothing in the configuration sets it)"))
    for line in obs["config_lines"][-MAX_SHOWN:]:
        c.note(line)

    extra = sorted(set(t for t in (obs["tables"] or {}))
                   - set(fixture["tables"]))
    if extra:
        c.note("%d table(s) the baseline never had, not scored: %s"
               % (len(extra), _shown(extra)))
    return c


def dumps(obj):
    return json.dumps(obj, sort_keys=True, indent=1, ensure_ascii=False)
