"""The encoding family: did the text survive latin1 -> UTF-8 intact?

This is the gate the task line asks for by name -- "zero invalid byte
sequences under `client_encoding=UTF8`", double-encoding detection, NFC
normalization -- and it is the one a migration gets wrong most quietly. A
dump-and-load that declares the wrong source charset produces a database that
is entirely valid UTF-8, answers every query, serves every page, and has
turned `Café` into `CafÃ©` in 32 places. Nothing short of comparing the values
notices.

So values are what is compared. `fixtures/textvalues-*.json` holds every
distinct non-ASCII value of every text column with the number of rows carrying
it, recorded from a running server (see `authoring/record_schema.py`) and
never transcribed from the SQL file.

Two modes, because one step in this task is *supposed* to change the text.

  preserve  Steps that move the data without reinterpreting it. Every recorded
            value must come back exactly as recorded -- mojibake included. A
            candidate who runs a repair tool early is not doing the step that
            was asked for, and loses points for it here.
  repair    The encoding-repair step and everything after it. The expectation
            is the *recorded* golden repair, taken from that step's solution
            tree the same way the baseline was taken.

The two modes do not share a denominator, and the reason is worth stating
because the first draft of this module got it wrong. Preserve mode asks one
question about all 62 text columns: is everything still exactly where it was?
Repair mode asks a different question about the few columns that actually hold
damage: was the damage undone, and undone properly? Scoring repair mode over
the preserve denominator was measured at a 0.73 floor for a tree where nothing
had been repaired at all -- because 44 of the 76 recorded values were never
broken and most columns hold no mojibake, so the step's own work was under a
fifth of its own score. Build-process item 6 wants that floor at or below 0.5.
Narrowing repair mode to the 14 columns that hold damage brings it to 0.395
over 268 checks, and every check in the narrower set is still something a
reader would want checked. Both figures are rehearsal measurements, from
`authoring/rehearse_gates.py`.

Which values "the step changes" is the diff of two recordings, never a list
written by hand. Each one is then asked four separate questions, because
partial repairs are the common failure and each catches a different one: the
broken form is gone; the repaired form is present in the right number of rows;
no *other* leftover in the column repairs to the same text (a half-undone
double encoding); and no other leftover normalises to it (an NFD remnant).

Reading is deliberately done twice over: once as text through the driver, and
once as bytes via `convert_to(..., 'UTF8')`. The first is what the application
sees and fails loudly if the database is `SQL_ASCII` holding latin1 bytes; the
second proves the stored octets really are UTF-8 rather than merely being
handed back unconverted. A database that passes both cannot be hiding invalid
sequences behind a no-op conversion.
"""

import io
import json

from . import gate
from . import mojibake

BASELINE = "/tests/fixtures/textvalues-baseline.json"
REPAIRED = "/tests/fixtures/textvalues-repaired.json"

PRESERVE = "preserve"
REPAIR = "repair"

# How many differing values to name per column before the report gives up.
MAX_SHOWN = 6


def fixture_for(mode):
    return REPAIRED if mode == REPAIR else BASELINE


def load(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def expected_columns(fixture):
    """[(table, column, {value: rows})] in the fixture's own order."""
    return [(c["table"], c["column"],
             dict((v, int(n)) for v, n in c["values"]))
            for c in fixture["columns"]]


def gone_values(baseline, fixture):
    """{(table, column): {broken form: rows}} -- what the repair removed.

    The diff of two recordings. A value both fixtures hold is not in here; a
    value the earlier recording had and the later one does not is something
    the repair step was measured to have taken away, and a tree that still
    contains it has not done the step.
    """
    later = dict(((c["table"].lower(), c["column"].lower()),
                  set(v for v, _ in c["values"])) for c in fixture["columns"])
    out = {}
    for col in baseline["columns"]:
        key = (col["table"].lower(), col["column"].lower())
        missing = dict((v, int(n)) for v, n in col["values"]
                       if v not in later.get(key, ()))
        if missing:
            out[(col["table"], col["column"])] = missing
    return out


# ------------------------------------------------------------ identifiers ---

def catalog(conn):
    """{(folded table, folded column): (real table, real column)}.

    A correct migration folds the four mixed-case table names to lower case,
    because the application creates and queries them unquoted. A candidate who
    quoted them into existence keeps the original case. Both are found here;
    whether the naming itself is acceptable is the schema gate's question.
    """
    out = {}
    cur = conn.cursor()
    cur.execute(
        "SELECT table_name, column_name, data_type FROM"
        " information_schema.columns WHERE table_schema = current_schema()")
    for table, column, data_type in cur.fetchall():
        out.setdefault((table.lower(), column.lower()),
                       (table, column, data_type))
    cur.close()
    return out


def _quote(ident):
    return '"%s"' % ident.replace('"', '""')


def reads(column, data_type):
    """(text expression, octet expression) for one column's PostgreSQL type.

    The doubled read described at the top of this module assumes the column
    is `text` or `varchar`. Two of the types a correct migration of this
    schema produces are neither, and reading them as text is a defect in the
    instrument rather than a finding about the tree -- the first pass of this
    module scored a perfect step-05 oracle at 314/328 for exactly that. §26.2.

      bytea       The migration's answer to MySQL's `utf8_bin` collation,
                  which PostgreSQL has no per-column equivalent for. Here the
                  octets *are* the column: `convert_to()` does not accept
                  bytea at all, and `convert_from()` cannot be used either,
                  because PostgreSQL's text type cannot hold a NUL byte and
                  jf_options.Value holds serialized session blobs that do --
                  it raises `invalid byte sequence for encoding "UTF8":
                  0x00` on a tree that is entirely correct. So read the
                  column once, as bytes, and decode it in Python. That is
                  also the more faithful form of the question the octet read
                  asks: are the stored octets UTF-8?
      character   `char(n)` comes back space-padded through the driver, while
                  the bpchar -> text cast that convert_to() applies first
                  strips the padding -- so the two reads disagree on every
                  row of every char column, for a reason that has nothing to
                  do with encoding. MySQL also strips trailing spaces from
                  CHAR on retrieval, so the cast is what the baseline
                  recorded: take it on both sides and the reads agree with
                  each other and with the fixture.
    """
    q = _quote(column)
    if data_type == "bytea":
        return q, q
    if data_type in ("character", "bpchar"):
        return "%s::text" % q, "convert_to(%s::text, 'UTF8')" % q
    return q, "convert_to(%s, 'UTF8')" % q


def read_column(conn, table, column, data_type=""):
    """({value: rows} over non-ASCII values, octets_ok), or raise.

    Raising is the point: a decode error here *is* the invalid-byte-sequence
    finding, and the caller turns it into failed checks rather than a skip.
    """
    text_expr, octet_expr = reads(column, data_type)
    cur = conn.cursor()
    try:
        cur.execute("SELECT %s, %s, count(*)"
                    " FROM %s WHERE %s IS NOT NULL GROUP BY 1, 2"
                    % (text_expr, octet_expr,
                       _quote(table), _quote(column)))
        rows = cur.fetchall()
    finally:
        cur.close()

    binary = data_type == "bytea"
    values, octets_ok = {}, True
    for text, octets, count in rows:
        raw = bytes(octets) if octets is not None else b""
        if binary:
            # Both expressions were the column itself; decode here.
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                octets_ok = False
                continue
        elif isinstance(text, (bytes, bytearray, memoryview)):
            raise UnicodeDecodeError("utf-8", bytes(text), 0, 1,
                                     "column returned raw bytes")
        else:
            try:
                if raw.decode("utf-8") != text:
                    octets_ok = False
            except UnicodeDecodeError:
                octets_ok = False
        if any(ord(ch) > 127 for ch in text):
            values[text] = values.get(text, 0) + int(count)
    return values, octets_ok


class _Reader(object):
    """Reads every wanted column once and remembers how each one went."""

    def __init__(self, conn, found):
        self.conn = conn
        self.found = found
        self.cache = {}

    def get(self, table, column):
        """(values, octets_ok, error). `error` non-empty means unusable."""
        key = (table.lower(), column.lower())
        if key in self.cache:
            return self.cache[key]
        real = self.found.get(key)
        if real is None:
            out = ({}, False, "no column %s.%s in the database"
                   % (table, column))
        else:
            try:
                values, octets_ok = read_column(self.conn, real[0], real[1],
                                                real[2])
                out = (values, octets_ok, "")
            except Exception as exc:                  # noqa: BLE001
                self.conn.rollback()
                out = ({}, False, "%s: %s" % (type(exc).__name__,
                                              str(exc)[:160]))
        self.cache[key] = out
        return out


# ---------------------------------------------------------- preserve mode ---

def _preserve_ids(columns):
    ids = ["encoding/database/encoding-utf8",
           "encoding/database/client-encoding-utf8",
           "encoding/database/mojibake", "encoding/database/nfc"]
    for table, column, values in columns:
        stem = "encoding/column/%s/%s/" % (table, column)
        ids += [stem + k for k in ("readable", "utf8-octets",
                                   "no-extra-values", "survey")]
        ids += ["encoding/value/%s/%s/%d" % (table, column, i)
                for i in range(len(values))]
    return ids


def _preserve(checks, reader, columns):
    seen = []
    for table, column, expect in columns:
        stem = "encoding/column/%s/%s/" % (table, column)
        own = ([stem + k for k in ("readable", "utf8-octets",
                                   "no-extra-values", "survey")]
               + ["encoding/value/%s/%s/%d" % (table, column, i)
                  for i in range(len(expect))])
        actual, octets_ok, error = reader.get(table, column)
        if error:
            checks.fail_all(own, error)
            continue

        checks.add(stem + "readable", True,
                   "%d non-ASCII value(s)" % len(actual))
        checks.add(stem + "utf8-octets", octets_ok,
                   "" if octets_ok else
                   "stored octets are not UTF-8 for the text returned")
        seen.extend(actual)

        extra = sorted(set(actual) - set(expect))
        checks.add(stem + "no-extra-values", not extra,
                   "unexpected non-ASCII values: %r" % (extra[:MAX_SHOWN],)
                   if extra else "")

        want = mojibake.survey(list(expect))
        got = mojibake.survey(list(actual))
        checks.add(stem + "survey",
                   (want["mojibake"], want["not_nfc"])
                   == (got["mojibake"], got["not_nfc"]),
                   "expected mojibake=%d not_nfc=%d, got mojibake=%d"
                   " not_nfc=%d" % (want["mojibake"], want["not_nfc"],
                                    got["mojibake"], got["not_nfc"]))

        for i, value in enumerate(sorted(expect)):
            hit = actual.get(value)
            checks.add("encoding/value/%s/%s/%d" % (table, column, i),
                       hit == expect[value],
                       "" if hit == expect[value] else
                       "%r expected in %d row(s), found %s"
                       % (value, expect[value],
                          "absent" if hit is None else "%d" % hit))

    # The damage must still be exactly the damage that was there. A step that
    # moves the data is not the step that repairs it.
    # Distinct values across the database on both sides. Counting the
    # expectation per column while counting the observation over the union
    # made the two disagree the moment one value appeared in two columns:
    # the NFD `Ångstrom` is seeded into app_user.Firstname and
    # jf_users.Username both, so want_n was 2 against a got_n of 1 on a
    # perfectly preserved tree. The question these two checks ask is a
    # database-wide one -- how many distinct damaged values are still
    # there -- so ask it the same way twice. §26.3.
    want = set(v for _, _, e in columns for v in e)
    want_m = sum(1 for v in want if mojibake.is_mojibake(v))
    want_n = sum(1 for v in want if not mojibake.is_nfc(v))
    got_m = sum(1 for v in set(seen) if mojibake.is_mojibake(v))
    got_n = sum(1 for v in set(seen) if not mojibake.is_nfc(v))
    checks.add("encoding/database/mojibake", got_m == want_m,
               "expected %d value(s) still carrying the original double"
               " encoding, found %d" % (want_m, got_m))
    checks.add("encoding/database/nfc", got_n == want_n,
               "expected %d non-NFC value(s) preserved, found %d"
               % (want_n, got_n))


# ------------------------------------------------------------ repair mode ---

def _repair_ids(columns, gone):
    """Only the columns that hold damage, plus two database-wide sweeps."""
    ids = ["encoding/database/encoding-utf8",
           "encoding/database/client-encoding-utf8",
           "encoding/database/mojibake", "encoding/database/nfc",
           "encoding/database/columns-readable",
           "encoding/database/octets-utf8"]
    for table, column, values in columns:
        if (table, column) not in gone:
            continue
        stem = "encoding/column/%s/%s/" % (table, column)
        ids += [stem + k for k in ("readable", "utf8-octets",
                                   "no-extra-values", "survey")]
        ids += ["encoding/value/%s/%s/%d" % (table, column, i)
                for i in range(len(values))]
        ids += ["encoding/repaired/%s/%s/%d/%s" % (table, column, i, k)
                for i in range(len(gone[(table, column)]))
                for k in ("gone", "present", "no-variant", "no-nfd")]
    return ids


def _repair(checks, reader, columns, gone):
    seen = []
    # The database-wide sweep: every text column must still be readable as
    # UTF-8 text and hold valid UTF-8 octets. Two checks rather than 124,
    # because in this mode the per-column detail is not what is being graded.
    unreadable, bad_octets = [], []
    for table, column, _ in columns:
        actual, octets_ok, error = reader.get(table, column)
        if error:
            unreadable.append("%s.%s (%s)" % (table, column, error[:60]))
        elif not octets_ok:
            bad_octets.append("%s.%s" % (table, column))
        seen.extend(actual)
    checks.add("encoding/database/columns-readable", not unreadable,
               "unreadable: %r" % (unreadable[:MAX_SHOWN],)
               if unreadable else "all %d text columns readable"
               % len(columns))
    checks.add("encoding/database/octets-utf8", not bad_octets,
               "octets are not UTF-8 in: %r" % (bad_octets[:MAX_SHOWN],)
               if bad_octets else "")

    for table, column, expect in columns:
        removed = gone.get((table, column))
        if not removed:
            continue
        stem = "encoding/column/%s/%s/" % (table, column)
        own = ([stem + k for k in ("readable", "utf8-octets",
                                   "no-extra-values", "survey")]
               + ["encoding/value/%s/%s/%d" % (table, column, i)
                  for i in range(len(expect))]
               + ["encoding/repaired/%s/%s/%d/%s" % (table, column, i, k)
                  for i in range(len(removed))
                  for k in ("gone", "present", "no-variant", "no-nfd")])
        actual, octets_ok, error = reader.get(table, column)
        if error:
            checks.fail_all(own, error)
            continue

        checks.add(stem + "readable", True,
                   "%d non-ASCII value(s)" % len(actual))
        checks.add(stem + "utf8-octets", octets_ok,
                   "" if octets_ok else
                   "stored octets are not UTF-8 for the text returned")
        extra = sorted(set(actual) - set(expect))
        checks.add(stem + "no-extra-values", not extra,
                   "unexpected non-ASCII values: %r" % (extra[:MAX_SHOWN],)
                   if extra else "")
        # Zero mojibake, and the recording's own count of unnormalised
        # values -- not zero. Repairing a latin1/UTF-8 round trip and
        # normalising Unicode are two different operations, and only the
        # first one is this migration. The corpus seeds `Ångstrom` twice,
        # NFC and NFD, as two separate accounts with two separate passwords
        # (authoring/seed/seedtool.php:194); `jf_users.Username` is uniquely
        # indexed, so normalising that column either collides on the index or
        # merges two users, and there is no non-arbitrary rule for which of
        # them survives. A gate that demanded NFC would be demanding data
        # loss. What it demands instead is that the damage is gone and
        # everything else is exactly where it was.
        got = mojibake.survey(list(actual))
        want_nfc = sum(1 for v in expect if not mojibake.is_nfc(v))
        checks.add(stem + "survey",
                   (got["mojibake"], got["not_nfc"]) == (0, want_nfc),
                   "mojibake=%d not_nfc=%d in this column, expected 0 and %d"
                   % (got["mojibake"], got["not_nfc"], want_nfc))

        for i, value in enumerate(sorted(expect)):
            hit = actual.get(value)
            checks.add("encoding/value/%s/%s/%d" % (table, column, i),
                       hit == expect[value],
                       "" if hit == expect[value] else
                       "%r expected in %d row(s), found %s"
                       % (value, expect[value],
                          "absent" if hit is None else "%d" % hit))

        for i, broken in enumerate(sorted(removed)):
            rstem = "encoding/repaired/%s/%s/%d/" % (table, column, i)
            # The repair alone. See the survey check above for why
            # this does not go on to normalise.
            fixed = mojibake.repair(broken)[0]
            still = actual.get(broken)
            checks.add(rstem + "gone", still is None,
                       "" if still is None else
                       "unrepaired %r still in %d row(s)" % (broken, still))

            want = expect.get(fixed)
            hit = actual.get(fixed)
            checks.add(rstem + "present",
                       want is not None and hit == want,
                       "" if (want is not None and hit == want) else
                       "%r expected in %s row(s), found %s"
                       % (fixed, want,
                          "absent" if hit is None else "%d" % hit))

            # A half-undone double encoding leaves a different wrong value
            # that still repairs to the same text.
            variants = sorted(v for v in actual if v != fixed
                              and v not in expect
                              and mojibake.repair(v)[0] == fixed)
            checks.add(rstem + "no-variant", not variants,
                       "partially repaired leftovers for %r: %r"
                       % (fixed, variants[:MAX_SHOWN]) if variants else "")

            # `v not in expect` on both: a spelling the golden tree
            # records is legitimate by construction, and the NFD
            # `Ångstrom` is one of them. Without the guard this check
            # would fire on the very value the survey check exists to keep.
            nfd = sorted(v for v in actual if v != fixed
                         and v not in expect
                         and mojibake.nfc(v) == mojibake.nfc(fixed))
            checks.add(rstem + "no-nfd", not nfd,
                       "unnormalised leftovers for %r: %r"
                       % (fixed, nfd[:MAX_SHOWN]) if nfd else "")

    # Over everything observed, not only what was recorded: a candidate who
    # repairs the known values but mangles another does not pass this.
    bad = sorted(v for v in set(seen) if mojibake.is_mojibake(v))
    checks.add("encoding/database/mojibake", not bad,
               "double encoding survives in %d value(s): %r"
               % (len(bad), bad[:MAX_SHOWN]) if bad else
               "no double-encoding signature in %d values" % len(set(seen)))
    # Symmetric, exactly as preserve mode's sweep is (§26.3): the
    # unnormalised values the golden tree records must still be there, and no
    # others. Counted distinct on both sides, because the NFD `Ångstrom`
    # is seeded into two columns and a per-column count would want 2 against
    # a database-wide got of 1.
    want_all = set(v for _, _, e in columns for v in e)
    want_n = sum(1 for v in want_all if not mojibake.is_nfc(v))
    unn = sorted(v for v in set(seen) if not mojibake.is_nfc(v))
    checks.add("encoding/database/nfc", len(unn) == want_n,
               "%d unnormalised value(s), expected %d: %r"
               % (len(unn), want_n, unn[:MAX_SHOWN])
               if len(unn) != want_n else
               "%d unnormalised value(s) preserved" % want_n)


# ------------------------------------------------------- the install data ---

# The checked-in PostgreSQL data, whose path `migrated:install-file` already
# pins. Read as text and scanned for the recorded spellings; the file is an
# INSERT script with single-quoted literals and no value in the corpus
# contains a quote or a backslash, so a substring match needs no SQL parser.
INSTALL_DATA = "/app/install/_db/pdo_pgsql.data.sql"

INSTALL_IDS = ("encoding/install/no-damage", "encoding/install/repaired")


def install_family(path=INSTALL_DATA, fixture=None, baseline=None,
                   family="migrated:install-repaired"):
    """Two checks on the checked-in data: the damage is out, the repair is in.

    This family exists for one reason the others do not share, and it is worth
    being explicit about it. Step 06's nop tree is a *finished* tree -- step
    05's solution, which passes every inherited guard -- so nothing inherited
    can put the nop at the 0.0 rule 8 requires. §24.14 met the same problem in
    step 05 and answered it the same way: keep one small, binary guard that
    states the step's headline requirement, name it in the instruction's first
    paragraph, and make sure every check in it is false on a tree that has not
    done the step. Both of these are.

    It is not a second copy of the repair family. That one reads the live
    database and never opens this file; `migrated:install-data` opens this file
    but only ever compares it *against* the live database, so a tree that
    repaired neither passes it. Between the three, repairing one of the two
    places is caught by install-data and repairing neither is caught here.
    """
    checks = gate.Checks(family)
    gone = gone_values(baseline, fixture)
    pairs = sorted((broken, mojibake.repair(broken)[0])
                   for col in gone.values() for broken in col)
    if not pairs:
        checks.fail_all(INSTALL_IDS,
                        "the two recordings name no repaired value, so this"
                        " family would be a mean over an empty set")
        return checks
    try:
        with io.open(path, "r", encoding="utf-8") as fh:
            text = fh.read()
    except (IOError, OSError) as exc:
        checks.fail_all(INSTALL_IDS, "%s could not be read: %s" % (path, exc))
        return checks
    except UnicodeDecodeError as exc:
        checks.fail_all(INSTALL_IDS,
                        "%s is not UTF-8: %s" % (path, exc))
        return checks

    left = sorted(broken for broken, _ in pairs if broken in text)
    checks.add(INSTALL_IDS[0], not left,
               "%d of %d damaged value(s) are still in %s: %r"
               % (len(left), len(pairs), path, left[:MAX_SHOWN])
               if left else
               "none of the %d damaged values is in %s" % (len(pairs), path))

    absent = sorted(fixed for _, fixed in pairs if fixed not in text)
    checks.add(INSTALL_IDS[1], not absent,
               "%d of %d repaired value(s) are missing from %s: %r"
               % (len(absent), len(pairs), path, absent[:MAX_SHOWN])
               if absent else
               "all %d repaired values are in %s" % (len(pairs), path))
    return checks


# ---------------------------------------------------------------- driving ---

def check_ids(mode, fixture=None, baseline=None):
    """The denominator, without touching a database."""
    fixture = fixture or load(fixture_for(mode))
    columns = expected_columns(fixture)
    if mode == REPAIR:
        baseline = baseline or load(BASELINE)
        return _repair_ids(columns, gone_values(baseline, fixture))
    return _preserve_ids(columns)


def evaluate(conn, mode, fixture=None, baseline=None, family="encoding"):
    """Score one deployment's text against the mode's recording.

    `family` is the plan's name for this reading, not this module's: the same
    instrument is an inherited guard in one step and the earned family in
    another, and `gate.combine` keys weights and guards off the name the part
    carries. Default kept as it was for the three steps already scored by it.
    """
    checks = gate.Checks(family)
    fixture = fixture or load(fixture_for(mode))
    columns = expected_columns(fixture)
    gone = {}
    if mode == REPAIR:
        baseline = baseline or load(BASELINE)
        gone = gone_values(baseline, fixture)
    every_id = (_repair_ids(columns, gone) if mode == REPAIR
                else _preserve_ids(columns))

    ok, problems = mojibake.selftest()
    if not ok:
        checks.error = "mojibake detector selftest failed"
        checks.fail_all(every_id, "; ".join(problems)[:400])
        return checks

    try:
        cur = conn.cursor()
        cur.execute("SHOW server_encoding")
        server_encoding = cur.fetchone()[0]
        cur.execute("SET client_encoding TO 'UTF8'")
        cur.execute("SHOW client_encoding")
        client_encoding = cur.fetchone()[0]
        cur.close()
        found = catalog(conn)
    except Exception as exc:                          # noqa: BLE001
        checks.error = "catalog unreadable: %s" % exc
        checks.fail_all(every_id, str(exc)[:200])
        return checks

    checks.add("encoding/database/encoding-utf8",
               server_encoding.upper().replace("-", "") == "UTF8",
               "server_encoding=%s" % server_encoding)
    checks.add("encoding/database/client-encoding-utf8",
               client_encoding.upper().replace("-", "") == "UTF8",
               "client_encoding=%s" % client_encoding)

    reader = _Reader(conn, found)
    if mode == REPAIR:
        _repair(checks, reader, columns, gone)
        checks.note("mode=repair columns-with-damage=%d values-removed=%d"
                    % (len(gone), sum(len(v) for v in gone.values())))
    else:
        _preserve(checks, reader, columns)
        checks.note("mode=preserve columns=%d values=%d"
                    % (len(columns), sum(len(e) for _, _, e in columns)))
    return checks


def crashed(reason, mode=PRESERVE, fixture=None, baseline=None,
            family="encoding"):
    """Zero over the real denominator when the deployment never came up."""
    try:
        n = len(check_ids(mode, fixture, baseline))
    except Exception:                                 # noqa: BLE001
        n = 100
    return gate.crashed(family, reason, n)
