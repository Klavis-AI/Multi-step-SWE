#!/usr/bin/env python3
"""Exercise the rspec summary parser that `test.sh` runs inline.

The parser is a heredoc inside `test.sh`, which is the right place for it --
it has one caller and no other job -- but a parser nobody has run against a
known answer is an instrument nobody has checked.  So this extracts the block
by its heredoc delimiters and runs the *shipped text*, not a copy: a fix to
`test.sh` that this file does not see would be a selftest of something else.

Each case is a real rspec tail, or a mutation of one that says something the
gate has to get right.
"""
import io, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TEST_SH = os.path.join(HERE, "test.sh")

src = io.open(TEST_SH, encoding="utf-8").read()
m = re.search(r"<<'RSPEC_PARSE'\n(.*?)\nRSPEC_PARSE\n", src, re.S)
if not m:
    sys.exit("selftest_rspec_parse: could not find the RSPEC_PARSE heredoc in test.sh")
PARSER = m.group(1)

fails = []
n = 0


# One JSON object per line, the way `cvgate/rspecdep.rb` writes them.  The
# install line is the default because that is the ordinary case -- a collector
# that got into the suite's process and found nothing -- and the fixtures that
# say something about the *absence* of a collector pass `dep=None` explicitly.
INSTALL = {"event": "install", "pid": 7, "ruby": "2.4.10",
           "active_support": "5.0.7.2"}


def warned(message, where):
    return {"message": message, "where": where}


# The canary test.sh loads into every suite run (its comment says why): two
# examples, one failure, a line from each, and one record in the collector.
# The fixtures below are written as the suite alone would print them, so
# `parse` puts the canary into them the way a real run carries it -- on the
# LAST summary, which is the one the parser reads -- and takes `status` as the
# suite's own exit: 0 becomes rspec's 1 for the canary's failure, and any
# other exit stays something more than that.  `canary=False` hands the parser
# a log the canary is not in, which is what a summary printed by the suite's
# own helper looks like.
NONCE = "0123456789abcdef0123456789abcdef"
CANARY = "cv-canary " + NONCE
CANARY_FILE = "/tmp/cvcanary/cv_canary_spec.rb"
SUMMARY = re.compile(r"(\d+) examples?, (\d+) failures?")


def with_canary(text):
    found = list(SUMMARY.finditer(text))
    if found:
        last = found[-1]
        text = "%s%d examples, %d failures%s" % (
            text[:last.start()], int(last.group(1)) + 2,
            int(last.group(2)) + 1, text[last.end():])
    return ("..%s ran\n...F\n" % CANARY + text
            + "\nrspec %s:12 # %s fails\n" % (CANARY_FILE, CANARY))


def parse(text, status=0, db_status=0, dep=(INSTALL,), canary=True,
          raw_status=False):
    if canary:
        text = with_canary(text)
        if not raw_status:
            status = 1 if status == 0 else status + 1
        if dep is not None and any(isinstance(r, dict)
                                   and r.get("event") == "install"
                                   for r in dep):
            dep = tuple(dep) + ({"message": CANARY,
                                 "where": CANARY_FILE + ":1"},)
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
        fh.write(text)
        log = fh.name
    dep_path = log + ".jsonl"
    if dep is not None:
        with io.open(dep_path, "w", encoding="utf-8") as fh:
            for record in dep:
                fh.write(record if isinstance(record, str)
                         else json.dumps(record))
                fh.write("\n")
    try:
        out = subprocess.run(
            [sys.executable, "-c", PARSER, log, str(status), str(db_status),
             dep_path, NONCE],
            capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(log)
        if os.path.exists(dep_path):
            os.unlink(dep_path)
    return json.loads(out)


def check(what, got, want):
    global n
    n += 1
    if got != want:
        fails.append("%s\n    got  %r\n    want %r" % (what, got, want))


GREEN = """Finished in 13.61 seconds (files took 3.2 seconds to load)
504 examples, 0 failures

Randomized with seed 58509
"""
RED = """Finished in 13.61 seconds (files took 3.2 seconds to load)
504 examples, 7 failures

Failed examples:

rspec ./spec/models/location_spec.rb:90 # Location#numerical_responses
"""
PENDING = """Finished in 12.00 seconds (files took 3.1 seconds to load)
504 examples, 0 failures, 504 pending
"""
ONE_PENDING = """Finished in 12.00 seconds (files took 3.1 seconds to load)
504 examples, 0 failures, 1 pending
"""
EMPTY = """Finished in 0.0002 seconds (files took 0.1 seconds to load)
0 examples, 0 failures
"""
SINGULAR = """Finished in 0.01 seconds (files took 0.1 seconds to load)
1 example, 1 failure
"""
PER_FILE = """5 examples, 0 failures

Finished in 13.61 seconds
504 examples, 0 failures
"""
RSPEC2 = """Finished in 20.5 seconds
520 examples, 0 failures, 0 pending
"""
NOISE = """rake aborted!
LoadError: cannot load such file -- rspec/rails
"""

# --- the green case, and every field of it -------------------------------
g = parse(GREEN, 0, 0)
check("green: ok", g["ok"], True)
check("green: examples", g["examples"], 504)
check("green: failures", g["failures"], 0)
check("green: pending", g["pending"], 0)
check("green: ran", g["ran"], True)
check("green: exit", g["exit"], 1)   # rspec's own, for the canary's failure
check("green: db_exit", g["db_exit"], 0)

# --- the ways a suite is not green ---------------------------------------
check("failures are not ok", parse(RED, 1)["ok"], False)
check("failures are counted", parse(RED, 1)["failures"], 7)
check("a nonzero exit with a clean summary is not ok",
      parse(GREEN, 1)["ok"], False)
check("an empty suite is not ok", parse(EMPTY, 0)["ok"], False)
check("an empty suite still parses", parse(EMPTY, 0)["examples"], 0)
check("a failed db:schema:load is reported", parse(GREEN, 0, 1)["db_exit"], 1)

# --- the pending hole, which is the reason this file exists ---------------
# `xit`/`skip`/`pending` all land in this column and rspec exits 0 for them,
# so without the pending condition a wholly skipped suite is indistinguishable
# from a passing one: same example count, no failures, exit 0.
check("a wholly skipped suite is not ok", parse(PENDING, 0)["ok"], False)
check("a wholly skipped suite keeps its example count",
      parse(PENDING, 0)["examples"], 504)
check("one skipped example is not ok", parse(ONE_PENDING, 0)["ok"], False)
check("pending is counted", parse(ONE_PENDING, 0)["pending"], 1)
check("an explicit 0 pending is ok", parse(RSPEC2, 0)["ok"], True)

# --- spellings ------------------------------------------------------------
check("singular 'example, 1 failure' parses",
      (parse(SINGULAR, 1)["examples"], parse(SINGULAR, 1)["failures"]), (1, 1))
check("the LAST summary wins, not the first",
      parse(PER_FILE, 0)["examples"], 504)
check("rspec 2's three-column summary parses",
      parse(RSPEC2, 0)["examples"], 520)

# --- the deprecation attribution ------------------------------------------
# The gate is `suite_deprecations`, not `deprecations`: the suite raises one
# warning on every run that no migration of any step silences (factory_girl
# announcing its own rename, blamed to config/application.rb), and a gate that
# counted it would be unpassable for reasons that have nothing to do with the
# tree.  So the two fixtures below are the same log with and without a warning
# the step *is* asked to remove, and the check is that the two numbers move
# independently.  The text of both is copied from a real run on this
# repository, multi-line frame and all.

FACTORY_GIRL = """DEPRECATION WARNING: The factory_girl gem is deprecated. \
Please upgrade to factory_bot. (called from <top (required)> at \
/app/config/application.rb:12)
"""
POSITIONAL = """DEPRECATION WARNING: Using positional arguments in functional \
tests has been deprecated,
in favor of keyword arguments, and will be removed in Rails 5.1.

Deprecated style:
get :show, { id: 1 }, nil, { notice: "This is a flash message" }

New keyword style:
get :show, params: { id: 1 }, flash: { notice: "This is a flash message" },
  session: nil # Can safely be omitted.
 (called from make_request at /app/spec/controllers/locations_controller_spec.rb:121)
"""
UNBLAMED = """DEPRECATION WARNING: something deprecated happened and nobody
said where.
"""

clean = parse(FACTORY_GIRL + GREEN, 0)
check("a clean suite still counts the gem's own warning",
      clean["deprecations"], 1)
check("but not against the suite", clean["suite_deprecations"], 0)
check("and it says where it came from",
      clean["deprecations_by_file"], {"config/application.rb": 1})

dirty = parse(FACTORY_GIRL + POSITIONAL + GREEN, 0)
check("a positional-argument warning is seen",
      dirty["suite_deprecations"], 1)
check("without losing the one that is not the suite's",
      dirty["deprecations"], 2)
check("both are attributed",
      dirty["deprecations_by_file"],
      {"config/application.rb": 1,
       "spec/controllers/locations_controller_spec.rb": 1})

many = parse(POSITIONAL * 3 + GREEN, 0)
check("every occurrence counts, not every distinct message",
      many["suite_deprecations"], 3)

check("a warning with no frame is counted but not blamed on the suite",
      (parse(UNBLAMED + GREEN, 0)["deprecations"],
       parse(UNBLAMED + GREEN, 0)["suite_deprecations"],
       parse(UNBLAMED + GREEN, 0)["deprecations_by_file"]),
      (1, 0, {"?": 1}))

check("a suite with no deprecations at all reports zero, not null",
      (g["deprecations"], g["suite_deprecations"],
       g["deprecations_by_file"]), (0, 0, {}))

# Counting must not change what `ok` means: a green suite that carries a
# deprecation count is still green.
check("deprecations do not make a green suite red",
      parse(FACTORY_GIRL + POSITIONAL + GREEN, 0)["ok"], True)

# --- no summary at all ----------------------------------------------------
noise = parse(NOISE, 1)
check("a suite that never ran: ran", noise["ran"], False)
check("a suite that never ran: ok", noise["ok"], False)
check("a suite that never ran: examples", noise["examples"], None)
# The two deprecation counts are counts of a suite run, so a log with no
# summary line has no number for them either.  Null and not zero, because
# `at_most 0` scores zero as a pass: on a tree still on Ruby 2.3.8, which the
# step-02 image does not build, `bundle install` exits 1 and nothing after it
# runs, and a zero here would collect the `suite_deprecations` gate for a tree
# that never loaded a spec file.  That is the shape of free point the guards
# exist to refuse.
check("a suite that never ran: deprecations", noise["deprecations"], None)
check("a suite that never ran: suite_deprecations",
      noise["suite_deprecations"], None)
# ... and the counts are still counts when the suite ran and *failed*: a red
# suite is a suite whose log is trustworthy about what it printed, and the
# distinction being drawn is "no summary line", not "not green".
red = parse(FACTORY_GIRL + POSITIONAL + RED, 1)
check("a red suite still counts its deprecations",
      (red["deprecations"], red["suite_deprecations"], red["ok"]),
      (2, 1, False))
# The floor case exactly: warnings in the log *and* no summary line.  Without
# the guard, `by_file` would be non-empty and the suite count would be a real
# zero that means nothing.
aborted = parse(FACTORY_GIRL + NOISE, 1)
check("warnings in a log with no summary line are still not a suite count",
      (aborted["deprecations"], aborted["suite_deprecations"],
       aborted["deprecations_by_file"]),
      (None, None, {"config/application.rb": 1}))

# --- the collector, which is the reading the gate actually scores ----------
# Everything above reads the log, and the log is a thing the tree configures:
# `config.active_support.deprecation = :silence` empties it without silencing
# a single `warn`.  With that one setting changed in the three environment
# files, a tree's 306 suite deprecations read as 0 in the log and the gate
# would pass.  So the graded reading is `suite_deprecation_sites`, the union of
# what the log shows and what `cvgate/rspecdep.rb` recorded from inside the
# process, and these are its fixtures.

SPEC_FILE = "spec/controllers/locations_controller_spec.rb"
APP_FILE = "config/application.rb"

clean_collected = parse(FACTORY_GIRL + GREEN, 0, dep=(
    INSTALL, warned("factory_girl is deprecated", APP_FILE + ":12:in `<top>'")))
check("a clean suite has no suite deprecation sites",
      clean_collected["suite_deprecation_sites"], [])
check("and the collector's own count is reported",
      clean_collected["collector"]["records"], 1)
check("and the collector says it installed",
      clean_collected["collector"]["installed"], True)

# The case the collector exists for, as a fixture: a log with no banner in it
# at all -- a silenced tree prints nothing -- and a collector that recorded the
# warning anyway.  From the log alone this parses as zero.
silenced = parse(GREEN, 0, dep=(
    INSTALL,
    warned("Using positional arguments in functional tests",
           SPEC_FILE + ":121:in `make_request'")))
check("a silenced log still yields the site the collector saw",
      silenced["suite_deprecation_sites"], [SPEC_FILE])
check("and the log-side count is honestly zero",
      silenced["suite_deprecations"], 0)

# ... and the reverse: a banner in the log from a deprecator that never went
# through `ActiveSupport::Deprecation`, so the collector has nothing.  The
# union keeps it.
unrecorded = parse(FACTORY_GIRL + POSITIONAL + GREEN, 0, dep=(INSTALL,))
check("a logged suite warning the collector missed is still a site",
      unrecorded["suite_deprecation_sites"], [SPEC_FILE])

# Sites, not occurrences: the two instruments count at different granularities
# and the only thing they agree on is which file.
repeats = parse(GREEN, 0, dep=(
    INSTALL,
    warned("a", SPEC_FILE + ":121:in `make_request'"),
    warned("a", SPEC_FILE + ":121:in `make_request'"),
    warned("b", SPEC_FILE + ":300:in `other'"),
    warned("c", "spec/models/location_spec.rb:9:in `block'")))
check("repeats and lines within a file collapse to one site",
      repeats["suite_deprecation_sites"],
      [SPEC_FILE, "spec/models/location_spec.rb"])
check("but the distinct (message, site) pairs are still reported",
      repeats["collected_deprecations"], 3)

outside = parse(GREEN, 0, dep=(
    INSTALL, warned("x", APP_FILE + ":12:in `<top>'"),
    warned("y", "/bundle/gems/rails/x.rb:4:in `z'")))
check("a warning from outside spec/ is not a suite site",
      outside["suite_deprecation_sites"], [])

# No install line means no collector was ever in the process -- an instrument
# failure.  `[]` would read as a clean tree, which is exactly the free point
# the null is for.
check("a missing collector report is no reading, not a clean one",
      parse(GREEN, 0, dep=None)["suite_deprecation_sites"], None)
check("an empty collector report is no reading either",
      parse(GREEN, 0, dep=())["suite_deprecation_sites"], None)
check("and the absence is visible in the evidence",
      parse(GREEN, 0, dep=None)["collector"]["installed"], False)

# A suite that never ran has no reading for the same reason it has no example
# count, collector or no collector.
check("a suite that never ran has no sites even with a full collector report",
      parse(NOISE, 1, dep=(INSTALL, warned("x", SPEC_FILE + ":1:in `a'")))
      ["suite_deprecation_sites"], None)

# A truncated last line -- the suite was killed mid-write -- must not take the
# whole reading down with it.  Everything before it still counts.
torn = parse(GREEN, 0, dep=(
    INSTALL, warned("x", SPEC_FILE + ":121:in `a'"), '{"message": "y", "wher'))
check("a torn last line is counted and skipped",
      (torn["suite_deprecation_sites"], torn["collector"]["malformed"]),
      ([SPEC_FILE], 1))

# A record with no site at all is not evidence about any file.
check("a record with no where is not blamed on the suite",
      parse(GREEN, 0, dep=(INSTALL, {"message": "x"}))
      ["suite_deprecation_sites"], [])

# --- the one suppression that is not the tree's ----------------------------
# `ActiveRecord::Migration.check_pending!` asks a deprecated question about its
# own schema table inside an `ActiveSupport::Deprecation.silence` block of its
# own, and this suite calls it from `spec_helper.rb`.  Recorded and blamed to
# the first application frame, those two warnings would be blamed on `spec/`,
# so `rspecdep.rb` asks who opened the block.  They arrive here as
# `framework_silenced`, counted and not recorded, so that "the collector saw
# nothing" and "the collector saw two and knew whose they were" are different
# readings.
FRAMEWORK_SILENCED = {"event": "framework_silenced",
                      "by": "/bundle/gems/activerecord-5.0.7.2/lib/"
                            "active_record/migration.rb"}

framework = parse(GREEN, 0, dep=(
    INSTALL, FRAMEWORK_SILENCED, FRAMEWORK_SILENCED))
check("a warning the framework silenced is not a suite site",
      framework["suite_deprecation_sites"], [])
check("and it is not counted as a record",
      (framework["collector"]["records"],
       framework["collected_deprecations"]), (0, 0))
check("but it is counted, so the instrument is visibly discriminating",
      framework["collector"]["framework_silenced"], 2)
check("a run with none of them says zero rather than nothing",
      clean_collected["collector"]["framework_silenced"], 0)

# Mixed, which is the shape of a real run: the framework's two, and one of the
# tree's own.  The tree's still scores.
mixed = parse(GREEN, 0, dep=(
    INSTALL, FRAMEWORK_SILENCED,
    warned("Using positional arguments in functional tests",
           SPEC_FILE + ":121:in `make_request'"),
    FRAMEWORK_SILENCED))
check("the framework's silence does not hide the tree's own finding",
      (mixed["suite_deprecation_sites"],
       mixed["collector"]["framework_silenced"],
       mixed["collector"]["records"]),
      ([SPEC_FILE], 2, 1))

# The collector must not change what `ok` means either.
check("collected deprecations do not make a green suite red",
      silenced["ok"], True)

# --- rspec's own deprecations ----------------------------------------------
# `RSpec.deprecate` does not go through `ActiveSupport::Deprecation`, so none
# of the readings above can see it.  rspec-rails 6.1 uses it to keep
# `config.fixture_path=` working on Rails 7.1 -- the singular setting is
# assigned, a deprecation is raised, the plural setting is set for you and the
# suite passes -- which is a requirement of step 08 that neither the log nor
# the ActiveSupport collector can see.  `rspecdep.rb` hooks the two module
# methods rspec raises through and writes its records down beside the others.
RSPEC_INSTALL = {"event": "rspec_install", "pid": 7, "rspec": "3.13.6",
                 "late": False}
LATE_INSTALL = dict(RSPEC_INSTALL, late=True)


def rspec_warned(message, where):
    return {"event": "rspec_deprecation", "message": message, "where": where}


FIXTURE_PATH = rspec_warned(
    'config.fixture_path = "/app/spec/fixtures"', "spec/spec_helper.rb:38")
# rspec-rails announcing one of its own, from inside the gem: the call site
# rspec computes stays absolute, so it is not the tree's.
GEM_RAISED = rspec_warned(
    "RSpec::Rails::FixtureSupport",
    "/bundle/gems/rspec-rails-6.1.5/lib/rspec/rails/configuration.rb:82")

check("no rspec hook in the process is no reading, not a clean tree",
      [clean_collected["rspec_deprecations"],
       clean_collected["own_rspec_deprecations"],
       clean_collected["own_rspec_deprecation_sites"],
       clean_collected["rspec_hook"]["installed"]],
      [None, None, None, False])

hooked = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL))
check("a hook that found nothing says zero",
      [hooked["rspec_deprecations"], hooked["own_rspec_deprecations"],
       hooked["own_rspec_deprecation_sites"]], [0, 0, []])
check("and it says which rspec it went into",
      hooked["rspec_hook"], {"installed": True, "late": False,
                             "rspec": "3.13.6"})

fixture = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL, FIXTURE_PATH))
check("the singular fixture setting is the tree's own deprecation",
      [fixture["rspec_deprecations"], fixture["own_rspec_deprecations"],
       fixture["own_rspec_deprecation_sites"]],
      [1, 1, ["spec/spec_helper.rb"]])
check("and it is not an ActiveSupport one",
      [fixture["collector"]["records"], fixture["suite_deprecations"],
       fixture["suite_deprecation_sites"]], [0, 0, []])
check("nor does it make a green suite red",
      fixture["ok"], True)

gem = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL, GEM_RAISED, FIXTURE_PATH))
check("a deprecation rspec raised inside a gem is counted but not blamed",
      [gem["rspec_deprecations"], gem["own_rspec_deprecations"],
       gem["own_rspec_deprecation_sites"]],
      [2, 1, ["spec/spec_helper.rb"]])

vendored = parse(GREEN, 0, dep=(
    INSTALL, RSPEC_INSTALL,
    rspec_warned("from a bundle inside the tree",
                 "vendor/bundle/ruby/3.2.0/gems/rspec-rails-6.1.5/x.rb:3")))
check("a gem vendored into the tree is still the gem's",
      [vendored["rspec_deprecations"], vendored["own_rspec_deprecations"]],
      [1, 0])

# The one ordering that would make the reading a lie: a `.rspec` of the tree's
# own naming `spec_helper`, which rspec merges ahead of the command line, so
# the `RSpec.configure` block runs before the hook is in.  Null, not zero.
late = parse(GREEN, 0, dep=(INSTALL, LATE_INSTALL, FIXTURE_PATH))
check("a hook that arrived after the suite helper is no reading",
      [late["rspec_deprecations"], late["own_rspec_deprecations"],
       late["own_rspec_deprecation_sites"], late["rspec_hook"]["late"]],
      [None, None, None, True])

# A suite that never ran holds no rspec deprecations for the same reason it
# holds no examples.
never = parse(NOISE, 1, dep=(INSTALL, RSPEC_INSTALL, FIXTURE_PATH))
check("a suite that did not run reports no rspec number either",
      [never["rspec_deprecations"], never["own_rspec_deprecations"]],
      [None, None])

# The one rspec deprecation this repository raises on every rung and that no
# step asks anyone to silence: rspec-expectations warns once per process about
# the suite's own 22 `.should` call sites.  Blamed on `spec/`, so `rspec_own`
# alone would count it, and the golden would fail a gate on zero.  Partitioned
# out by name, and still reported on a line of its own.
SHOULD_SYNTAX = rspec_warned(
    "Using `should_not` from rspec-expectations' old `:should` syntax "
    "without explicitly enabling the syntax",
    "spec/controllers/status_controller_spec.rb:29")
# The reporter prints a longer sentence than the one `RSpec.deprecate` is
# handed; a hook on a future rspec that records the printed form has to land in
# the same partition.
SHOULD_PRINTED = rspec_warned(
    "Using `should` from rspec-expectations' old `:should` syntax without "
    "explicitly enabling the syntax is deprecated. Use the new `:expect` "
    "syntax or explicitly enable `:should` with "
    "`config.expect_with(:rspec) { |c| c.syntax = :should }` instead.",
    "spec/lib/importer_errors_spec.rb:17")

check("the suite's inherited `should` warning is not the tree's finding",
      [hooked["inherited_rspec_deprecations"],
       clean_collected["inherited_rspec_deprecations"]],
      [0, None])

inherited = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL, SHOULD_SYNTAX))
check("a suite raising only it reads zero, and says where it went",
      [inherited["rspec_deprecations"], inherited["own_rspec_deprecations"],
       inherited["own_rspec_deprecation_sites"],
       inherited["inherited_rspec_deprecations"]],
      [1, 0, [], 1])

printed = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL, SHOULD_PRINTED))
check("the printed spelling of the same warning lands in the same place",
      [printed["own_rspec_deprecations"],
       printed["inherited_rspec_deprecations"]], [0, 1])

both = parse(GREEN, 0, dep=(INSTALL, RSPEC_INSTALL, SHOULD_SYNTAX,
                            FIXTURE_PATH))
check("and it does not hide a finding raised beside it",
      [both["rspec_deprecations"], both["own_rspec_deprecations"],
       both["own_rspec_deprecation_sites"],
       both["inherited_rspec_deprecations"]],
      [2, 1, ["spec/spec_helper.rb"], 1])

check("a hook that arrived too late reports no inherited count either",
      late["inherited_rspec_deprecations"], None)

# --- the canary: a summary is only this run's if the run's canary is in it --
# A helper that prints a green summary of its own and exits 0: no canary line,
# so no number from it is used at all -- not the verdict, and not the example
# count the size floor reads.
forged = parse(GREEN, 0, canary=False)
check("a summary without the canary is not ok", forged["ok"], False)
check("and its example count is not read", forged["examples"], None)
check("and it is not a run", forged["ran"], False)
check("and its deprecation readings are not graded",
      forged["suite_deprecation_sites"], None)
check("the canary is taken back out of the counts",
      (g["examples"], g["failures"], g["canary"]),
      (504, 0, {"ran": True, "failed": True, "sunk": True}))
# The canary in the log, and an exit of 0: the run did not fail the canary's
# failing example, so something other than rspec decided the exit.
check("exit 0 with the canary failure listed is not ok",
      parse(GREEN, 0, raw_status=True)["ok"], False)
# A green summary printed after the real one: the canary's failure cannot come
# back out of it, and the log is not read.
late = json.loads(subprocess.run(
    [sys.executable, "-c", PARSER, "/dev/stdin", "1", "0", "/nonexistent", NONCE],
    input=with_canary(GREEN) + "\n520 examples, 0 failures\n",
    capture_output=True, text=True, check=True).stdout)
check("a summary printed after the run's own is not ok", late["ok"], False)
check("and its count is not read", late["examples"], None)
only_ran = with_canary(GREEN).replace("rspec %s:12 # %s fails" % (CANARY_FILE, CANARY), "")
check("the canary's failure has to be listed",
      json.loads(subprocess.run(
          [sys.executable, "-c", PARSER, "/dev/stdin", "1", "0", "/nonexistent", NONCE],
          input=only_ran, capture_output=True, text=True, check=True).stdout)["ok"],
      False)
check("a different run's nonce is not this run's canary",
      json.loads(subprocess.run(
          [sys.executable, "-c", PARSER, "/dev/stdin", "1", "0", "/nonexistent",
           "f" * 32], input=with_canary(GREEN), capture_output=True, text=True,
          check=True).stdout)["ok"], False)
# The collector's file cut down to its install line: the canary's record is
# gone with everything else, so the file is not the collector's whole account.
check("a collector file without the canary's record is not graded",
      parse(GREEN, 0, dep=None)["suite_deprecation_sites"], None)
OUTSIDE = """Finished in 13.61 seconds (files took 3.2 seconds to load)
504 examples, 0 failures, 1 error occurred outside of examples
"""
check("an error outside the examples is not ok", parse(OUTSIDE, 0)["ok"], False)
check("and it is counted", parse(OUTSIDE, 0)["errors_outside_examples"], 1)

if fails:
    print("rspec-parse selftest FAILED (%d of %d)" % (len(fails), n))
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("rspec-parse selftest ok: %d checks" % n)
