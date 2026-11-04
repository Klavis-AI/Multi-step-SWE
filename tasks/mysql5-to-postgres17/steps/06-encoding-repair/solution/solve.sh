#!/usr/bin/env bash
#
# Reference solution for step 06 -- repair the text.
#
# Steps 01-05 moved this deployment to PostgreSQL 17 without reinterpreting a
# single stored byte, which is what they were graded on. This step looks at the
# bytes. Some of the text in this database went through a latin1/UTF-8 round
# trip long before the migration saw it -- UTF-8 written, read back a byte at a
# time as a single-byte charset, and stored as the characters that reading
# produced -- and a faithful carry of 'Caf\xc3\xa9' is still 'Caf\xc3\xa9'.
#
# Three questions decide whether this is a repair or a second round of damage.
#
#   WHICH single-byte charset. Not a free choice, and not one charset. MySQL's
#   `latin1` is CP1252 with the five bytes CP1252 leaves undefined mapped to
#   the C1 control at the same code point; a client that asked for ISO-8859-1
#   by its real name, or a pipeline through PHP's utf8_decode, produced
#   ISO-8859-1 proper instead. Neither subsumes the other. '\xc3\x85sa' needs
#   U+0085 re-encoded to the byte 0x85, which MySQL's latin1 spends on an
#   ellipsis and so cannot produce; the Greek and Russian values need U+0152
#   re-encoded to 0x8c, which ISO-8859-1 has no byte for at all. So both, in
#   that order, first one that works wins -- and one value in this corpus needs
#   one of each on successive rounds.
#
#   WHICH values. Most of the non-ASCII text here is correct and outnumbers the
#   damage. Two properties separate them without any guess about what text
#   ought to look like: the re-encoded bytes must be valid UTF-8, and the
#   decoded result must differ from the input. The first is what leaves
#   'S\xe3o Paulo – SP', 'Truncated Caf\xc3' and 'Prize\x81\x8d\x8f'
#   alone -- each re-encodes to bytes that are not UTF-8 at all. The second is
#   why pure ASCII can never be flagged.
#
#   HOW MANY TIMES. Some values went through the accident twice and read back
#   as '\xc3ƒ\xc2\xbc' where a U+00FC belongs. Repair to a fixed point.
#
# And one thing this deliberately does not do: normalise. `jf_users` holds the
# NFC and the NFD spelling of the same name as two separate accounts with two
# separate passwords, and `Username` is uniquely indexed -- normalising that
# column either raises on the index or merges two users on a tie-break this
# application does not have. Both spellings are correct text. Repairing an
# encoding and normalising Unicode are different operations, and only the first
# one is this step.
#
# The repair lands in two places the verifier checks against each other: the
# live database, and install/_db/pdo_pgsql.data.sql, which was generated from
# it. Every stage asserts what it expected to find (trap 7) -- a repair that
# matched nothing looks exactly like a repair that worked, unless the count is
# checked.
set -euo pipefail

APP=/app
PGPORT=5432
PGSOCK=/var/run/postgresql
DB=webgoatphp
DATA=$APP/install/_db/pdo_pgsql.data.sql

echo "== step 06: repair the text"

psu() { su -s /bin/sh postgres -c "$*"; }

/usr/local/sbin/wg-services start
for i in $(seq 1 60); do
    psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SELECT 1'" >/dev/null 2>&1 && break
    sleep 1
done
psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT 1'" >/dev/null

SCRATCH=$(mktemp -d /tmp/wg-step06.XXXXXX)
trap 'rm -rf "$SCRATCH"' EXIT
chmod 755 "$SCRATCH"

# --------------------------------------------------------- the detector ----
cat > "$SCRATCH/mojibake.py" <<'PY'
"""Undo a latin1/UTF-8 round trip, and decide which values had one."""

# The char -> byte direction of MySQL's `latin1` over 0x80-0x9f, derived from
# the cp1252 codec rather than typed out: cp1252 for the 27 bytes it defines,
# and the same-numbered C1 control for the five it does not (0x81 0x8d 0x8f
# 0x90 0x9d), which is exactly what MySQL maps them to.
_ENC = {}
for _b in range(0x80, 0xA0):
    try:
        _ch = bytes([_b]).decode("cp1252")
    except UnicodeDecodeError:
        _ch = chr(_b)
    _ENC[_ch] = _b


def _mysql_latin1(text):
    """The bytes MySQL's latin1 would have stored, or None if it could not."""
    out = bytearray()
    for ch in text:
        cp = ord(ch)
        if cp < 0x80 or 0xA0 <= cp < 0x100:
            out.append(cp)
        elif ch in _ENC:
            out.append(_ENC[ch])
        else:
            return None
    return bytes(out)


def _latin1(text):
    """ISO-8859-1 proper: no cp1252 punctuation, all of 0x80-0x9f available."""
    try:
        return text.encode("latin-1")
    except UnicodeEncodeError:
        return None


def undo(text):
    """One round of repair, or None when this value was never misread."""
    for enc in (_mysql_latin1, _latin1):
        raw = enc(text)
        if raw is None:
            continue
        try:
            out = raw.decode("utf-8")
        except UnicodeDecodeError:
            # The re-encoding is not UTF-8, so it was not UTF-8 that got
            # misread; this value is what it says it is. Leaving it alone here
            # is the whole reason correct non-ASCII text survives.
            continue
        if out != text:
            return out
    return None


def repair(text):
    """The fixed point. A value that was never damaged comes back unchanged."""
    for _ in range(6):
        nxt = undo(text)
        if nxt is None:
            break
        text = nxt
    return text
PY

# The detector has to be right about what it leaves alone, not only about what
# it changes, so it answers for known values before it is pointed at the data.
python3 - "$SCRATCH" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import mojibake

repaired = [
    ("Caf\xc3\xa9 voucher", "Caf\xe9 voucher"),            # one round
    ("Z\xc3ƒ\xc2\xbcrich", "Z\xfcrich"),              # two rounds
    ("\xc3\x85sa", "\xc5sa"),                              # needs ISO-8859-1
    ("\xc3ƒ\xc2…kerlund", "\xc5kerlund"),         # one of each
    ("\xce˜\xce\xb5\xcf\x81\xce\xb9\xce\xbd\xcfŒ",
     "Θερινό"),              # needs cp1252
]
untouched = [
    "S\xe3o Paulo – SP",   # re-encodes to bytes that are not UTF-8
    "Truncated Caf\xc3",        # the continuation byte is missing
    "Prize\x81\x8d\x8f",        # the bytes cp1252 leaves undefined
    "Legacy note 6: half a character \xc3 and a stray \xa9 byte",
    "\xa9dmin", "\xc6\xd8\xc5", "Gr\xfc\xdfe", "Mar\xeda",
    "日本語", "İstanbul", "ﬁligree",
    "“Quoted” and ‘quoted’ … with an em—dash",
    "\xc5ngstrom", "Ångstrom",   # NFC and NFD: both correct, both stay
    "plain ascii", "",
]
bad = []
for broken, want in repaired:
    got = mojibake.repair(broken)
    if got != want:
        bad.append("repair(%s) = %s, want %s"
                   % (ascii(broken), ascii(got), ascii(want)))
for value in untouched:
    got = mojibake.repair(value)
    if got != value:
        bad.append("repair(%s) = %s, want it unchanged"
                   % (ascii(value), ascii(got)))
if bad:
    sys.exit("detector selftest failed:\n  " + "\n  ".join(bad))
print("  detector selftest ok: %d repaired, %d left alone"
      % (len(repaired), len(untouched)))
PY

# ------------------------------------------------------ what to look at ----
# Every text column of every base table the catalog knows about, not a list
# someone typed: a repair aimed at the columns you remembered is a repair with
# a hole in it.
cat > "$SCRATCH/columns.sql" <<'SQL'
SELECT c.table_name || '|' || c.column_name
  FROM information_schema.columns c
  JOIN information_schema.tables t
    ON t.table_schema = c.table_schema AND t.table_name = c.table_name
 WHERE c.table_schema = 'public'
   AND t.table_type = 'BASE TABLE'
   AND c.data_type IN ('text', 'character varying', 'character')
 ORDER BY c.table_name, c.column_name;
SQL
chmod 644 "$SCRATCH/columns.sql"
psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atq -v ON_ERROR_STOP=1 -f $SCRATCH/columns.sql" \
    > "$SCRATCH/columns.txt"
echo "  text columns: $(wc -l < "$SCRATCH/columns.txt")"
test -s "$SCRATCH/columns.txt" || { echo "no text columns found" >&2; exit 1; }

# One statement per column, each emitting a single JSON object. JSON because a
# stored value may contain a newline or a tab, and a line-oriented dump would
# hand half of it to the detector.
python3 - "$SCRATCH" <<'PY'
import io, os, sys
scratch = sys.argv[1]
lines = [l for l in io.open(os.path.join(scratch, "columns.txt"),
                            encoding="utf-8").read().split("\n") if l]
out = []
for line in lines:
    table, column = line.split("|", 1)
    out.append(
        "SELECT json_build_object('t', %s, 'c', %s, 'v',"
        " coalesce(json_agg(DISTINCT x.v), '[]'::json))::text"
        " FROM (SELECT \"%s\"::text AS v FROM \"%s\""
        " WHERE \"%s\" IS NOT NULL) x;"
        % ("'" + table.replace("'", "''") + "'",
           "'" + column.replace("'", "''") + "'",
           column.replace('"', '""'), table.replace('"', '""'),
           column.replace('"', '""')))
io.open(os.path.join(scratch, "dump.sql"), "w", encoding="utf-8").write(
    "\n".join(out) + "\n")
PY
chmod 644 "$SCRATCH/dump.sql"

dump() {
    psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atq -v ON_ERROR_STOP=1 -f $SCRATCH/dump.sql" \
        > "$1"
}
dump "$SCRATCH/before.jsonl"

# ------------------------------------------------------ the substitutions --
# Found once and written down, so that the database and the installer file get
# the same answer and cannot drift apart.
python3 - "$SCRATCH" <<'PY'
import io, json, os, sys
scratch = sys.argv[1]
sys.path.insert(0, scratch)
import mojibake


def lit(s):
    return "'" + s.replace("'", "''") + "'"


subs, stmts, touched = {}, [], []
for line in io.open(os.path.join(scratch, "before.jsonl"), encoding="utf-8"):
    line = line.strip()
    if not line:
        continue
    col = json.loads(line)
    pairs = []
    for value in col["v"]:
        fixed = mojibake.repair(value)
        if fixed != value:
            pairs.append((value, fixed))
    if not pairs:
        continue
    touched.append("%s.%s (%d)" % (col["t"], col["c"], len(pairs)))
    for old, new in sorted(pairs):
        # The same damaged value shows up in more than one column; the repair
        # is the same wherever it appears, and a disagreement would mean the
        # detector is not a function of its input, so say so rather than pick.
        if subs.setdefault(old, new) != new:
            sys.exit("two different repairs for %s: %s and %s"
                     % (ascii(old), ascii(subs[old]), ascii(new)))
        stmts.append('UPDATE "%s" SET "%s" = %s WHERE "%s" = %s;'
                     % (col["t"].replace('"', '""'),
                        col["c"].replace('"', '""'), lit(new),
                        col["c"].replace('"', '""'), lit(old)))

if not stmts:
    sys.exit("found no damaged value anywhere, which is not what this"
             " deployment holds")
io.open(os.path.join(scratch, "repair.sql"), "w", encoding="utf-8").write(
    "BEGIN;\n" + "\n".join(stmts) + "\nCOMMIT;\n")
io.open(os.path.join(scratch, "subs.json"), "w", encoding="utf-8").write(
    json.dumps(subs, ensure_ascii=False, sort_keys=True, indent=1))
print("  damaged: %d distinct value(s) across %d column(s)"
      % (len(subs), len(touched)))
for t in touched:
    print("      %s" % t)
PY
chmod 644 "$SCRATCH/repair.sql"

# One transaction: a half-repaired database is worse than an unrepaired one.
psu "psql -p $PGPORT -h $PGSOCK -d $DB -q -v ON_ERROR_STOP=1 -f $SCRATCH/repair.sql"

# ------------------------------------------------------ the installer -----
test -f "$DATA" || { echo "missing $DATA" >&2; exit 1; }
python3 - "$SCRATCH" "$DATA" <<'PY'
import io, json, sys
scratch, data = sys.argv[1:3]
subs = json.load(io.open(scratch + "/subs.json", encoding="utf-8"))
text = io.open(data, encoding="utf-8").read()
# Longest first: one damaged value is a substring of another here, and
# replacing the short one first would leave the long one half repaired.
hits = 0
for old in sorted(subs, key=lambda s: (-len(s), s)):
    n = text.count(old)
    hits += n
    if n:
        text = text.replace(old, subs[old])
if not hits:
    sys.exit("no damaged value appears in %s, which cannot be right for a"
             " file generated from the database that just needed repairing"
             % data)
io.open(data, "w", encoding="utf-8").write(text)
print("  installer data: %d occurrence(s) rewritten" % hits)
PY

# ------------------------------------------------------ did it land? ------
dump "$SCRATCH/after.jsonl"
python3 - "$SCRATCH" "$DATA" <<'PY'
import io, json, sys
scratch, data = sys.argv[1:3]
sys.path.insert(0, scratch)
import mojibake

subs = json.load(io.open(scratch + "/subs.json", encoding="utf-8"))
problems = []

still = []
for line in io.open(scratch + "/after.jsonl", encoding="utf-8"):
    line = line.strip()
    if not line:
        continue
    col = json.loads(line)
    for value in col["v"]:
        if mojibake.repair(value) != value:
            still.append("%s.%s=%s" % (col["t"], col["c"], ascii(value)))
if still:
    problems.append("%d value(s) in the database are still damaged: %s"
                    % (len(still), ", ".join(still[:5])))

# The repaired forms have to be present, not merely the damage absent: an
# UPDATE that blanked the column would satisfy the first half on its own.
got = set()
for line in io.open(scratch + "/after.jsonl", encoding="utf-8"):
    line = line.strip()
    if line:
        got.update(json.loads(line)["v"])
lost = sorted(f for f in subs.values() if f not in got)
if lost:
    problems.append("%d repaired value(s) are not in the database: %s"
                    % (len(lost), ", ".join(ascii(v) for v in lost[:5])))

text = io.open(data, encoding="utf-8").read()
left = sorted(b for b in subs if b in text)
if left:
    problems.append("%d damaged value(s) still in %s: %s"
                    % (len(left), data, ", ".join(ascii(v) for v in left[:5])))
absent = sorted(f for f in subs.values() if f not in text)
if absent:
    problems.append("%d repaired value(s) missing from %s: %s"
                    % (len(absent), data,
                       ", ".join(ascii(v) for v in absent[:5])))

if problems:
    sys.exit("step 06 did not land:\n  " + "\n  ".join(problems))
print("  verified: %d repaired value(s), no damage left in the database or"
      " in %s" % (len(subs), data))
PY

/usr/local/sbin/wg-services start
echo "== step 06 done"
