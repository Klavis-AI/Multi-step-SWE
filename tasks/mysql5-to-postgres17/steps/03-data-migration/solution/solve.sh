#!/usr/bin/env bash
#
# Reference solution for step 03 -- "the data is in PostgreSQL".
#
# Step 02 built the tables; they are empty. This step fills them from the live
# MySQL database and leaves behind the file a repeatable migration needs:
#
#   /app/install/_db/pdo_pgsql.data.sql -- the rows, as the file
#   jf\BaseDatabase::GetSQL('data') derives from the adapter class name.
#   InitializeData() is the framework's own path for it, so the migrated data
#   ships the same way the migrated schema does and a fresh install of the
#   pdo_pgsql adapter comes up seeded -- at whatever table prefix that
#   installation is configured with, which is why every table name in the file
#   is written PREFIX_... and never jf_....
#
# Three things in here are the whole substance of the step, and each is a
# measurement rather than a preference:
#
#   * The conversion is MySQL's. The connection asks for `charset=utf8mb4`,
#     so the server transcodes every latin1 column on the way out. latin1 is
#     not ISO-8859-1 -- it is CP1252 plus five C1 controls -- and the seed data
#     holds bytes in 0x80-0x9F that iconv -f ISO-8859-1 turns into the wrong
#     characters. Letting the server that owns the declaration do the decoding
#     is the only way to be right about all of them without reimplementing the
#     table.
#
#   * jf_options.Value becomes bytea. One of its 371 rows holds a NUL byte,
#     which is a legal byte in a MySQL blob-ish text column and is not a legal
#     character in PostgreSQL `text` at any encoding. Everything else stays
#     text. The change is made in the schema file as well as in the live
#     database, because the schema file is what a fresh install runs.
#
#   * The file is ordered by the foreign keys. Six of them, and PostgreSQL
#     checks each on INSERT. `SET session_replication_role = replica` would
#     silence that, but it is a SUSET setting: it works for the superuser this
#     deployment happens to connect as and fails for any role that is not one.
#     A data file whose loadability depends on who runs it is a worse file than
#     one that is simply in the right order, so this one is in the right order.
#
# Nothing here repoints the application: it keeps reading MySQL until step 04.
set -euo pipefail

APP=/app
PGPORT=5432
PGSOCK=/var/run/postgresql
DB=webgoatphp
PREFIX=jf_
MYSOCK=/var/run/mysqld/mysqld.sock

SCHEMA=$APP/install/_db/pdo_pgsql.schema.sql
DATA=$APP/install/_db/pdo_pgsql.data.sql

echo "== step 03: the data moves to PostgreSQL 17"

psu() { su -s /bin/sh postgres -c "$*"; }
psql_() { psu "psql -p $PGPORT -h $PGSOCK -d $DB -v ON_ERROR_STOP=1 $*"; }

SCRATCH=$(mktemp -d /tmp/wg-step03.XXXXXX)
trap 'rm -rf "$SCRATCH"' EXIT
chmod 755 "$SCRATCH"

# ----------------------------------------------------------- 1. both servers --
/usr/local/sbin/wg-services start
for i in $(seq 1 60); do
    mysql --socket="$MYSOCK" -uroot -e 'SELECT 1' >/dev/null 2>&1 && break
    sleep 1
done
for i in $(seq 1 60); do
    psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SELECT 1'" >/dev/null 2>&1 && break
    sleep 1
done
mysql --socket="$MYSOCK" -uroot -N -B -e 'SELECT VERSION()'
psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT version()'"

# -------------------------------------------------------- 2. the NUL column --
# `SELECT ... WHERE Value LIKE '%\0%'` finds it in jf_options; the value is a
# serialised blob the framework stored through its options API. PostgreSQL
# rejects \x00 in text at every encoding, so the column's type is the one part
# of step 02's schema this step has to correct. jf__test_options is the same
# table under the test prefix and is corrected with it, so a test run cannot
# hit the failure the production table was just spared.
[ -f "$SCHEMA" ] || { echo "step 02's $SCHEMA is missing" >&2; exit 1; }
python3 - "$SCHEMA" <<'PY'
import io, sys

path = sys.argv[1]
old = '    value text COLLATE "C" NOT NULL,\n'
new = '    value bytea NOT NULL,\n'

with io.open(path, "r", encoding="utf-8") as fh:
    text = fh.read()

if text.count(new) == 2 and text.count(old) == 0:
    print("   %s already declares value bytea" % path)
    raise SystemExit(0)

# Assert the edit landed rather than trusting it: both options tables, no
# other column in the file is spelled this way.
if text.count(old) != 2:
    raise SystemExit("expected 2 occurrences of the options value column,"
                     " found %d" % text.count(old))
text = text.replace(old, new)
assert text.count(new) == 2, text.count(new)

with io.open(path, "w", encoding="utf-8") as fh:
    fh.write(text)
print("   %s: value text -> value bytea (2 tables)" % path)
PY

# The same change on the database step 02 already built, so the live cluster
# and the file that builds it stay the same schema. convert_to() is the cast
# that says what the old text's bytes were; the default cast would refuse.
cat > "$SCRATCH/alter.sql" <<'PGSQL'
DO $$
DECLARE t text;
BEGIN
    FOREACH t IN ARRAY ARRAY['jf_options', 'jf__test_options'] LOOP
        IF EXISTS (SELECT 1 FROM pg_attribute a
                     JOIN pg_class c ON c.oid = a.attrelid
                    WHERE c.relname = t AND a.attname = 'value'
                      AND a.atttypid = 'text'::regtype) THEN
            EXECUTE format('ALTER TABLE %I ALTER COLUMN value TYPE bytea'
                           ' USING convert_to(value, ''UTF8'')', t);
            RAISE NOTICE '% .value is now bytea', t;
        END IF;
    END LOOP;
END $$;
PGSQL
chmod 644 "$SCRATCH/alter.sql"
psql_ "-q -f '$SCRATCH/alter.sql'"

# ------------------------------------------------------- 3. generate the file --
cat > "$SCRATCH/gen.php" <<'GENPHP'
<?php
// Emits install/_db/pdo_pgsql.data.sql on stdout: the rows of the live MySQL
// database, in the form the PostgreSQL schema step 02 built will take them.
//
// Read as the `postgres` OS user so the PostgreSQL side is peer-authenticated
// (which is what the grader's forced hba grants too) while MySQL is reached
// with its own root/empty credentials over the socket.
$MYSOCK = "/var/run/mysqld/mysqld.sock";
$DB     = "webgoatphp";
$PREFIX = "jf_";
$BATCH  = 200;

$my = new PDO("mysql:unix_socket=$MYSOCK;dbname=$DB;charset=utf8mb4", "root", "");
$my->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$pg = new PDO("pgsql:host=/var/run/postgresql;port=5432;dbname=$DB;user=postgres");
$pg->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

// --- what PostgreSQL has, by folded name -------------------------------------
$pgcols = array();   // table -> column -> format_type
$q = $pg->query("SELECT c.relname, a.attname, format_type(a.atttypid, a.atttypmod)
                   FROM pg_class c
                   JOIN pg_namespace n ON n.oid = c.relnamespace
                   JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum > 0
                                      AND NOT a.attisdropped
                  WHERE c.relkind = 'r'
                    AND n.nspname NOT IN ('pg_catalog','information_schema')");
foreach ($q as $r) { $pgcols[$r[0]][$r[1]] = $r[2]; }

$ident = array();    // table -> column, the identity columns
$q = $pg->query("SELECT c.relname, a.attname
                   FROM pg_class c
                   JOIN pg_namespace n ON n.oid = c.relnamespace
                   JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum > 0
                  WHERE c.relkind = 'r' AND a.attidentity <> ''
                    AND n.nspname NOT IN ('pg_catalog','information_schema')");
foreach ($q as $r) { $ident[$r[0]][] = $r[1]; }

function pgname($t, $PREFIX) {
    $f = strtolower($t);
    return (strpos($f, $PREFIX) === 0)
        ? "PREFIX_" . substr($f, strlen($PREFIX)) : $f;
}

function lit($v, $type) {
    if ($v === null) return "NULL";
    if ($type === "bytea") return "'\\x" . bin2hex($v) . "'";
    return "'" . str_replace("'", "''", $v) . "'";
}

// --- the six foreign keys, as a load order -----------------------------------
// A child row cannot go in before the row it points at, and PostgreSQL checks
// that on every INSERT. So the file is ordered -- parents first, alphabetically
// within a tier, which is stable from one regeneration to the next.
$parents = array();
$q = $pg->query("SELECT t.relname, r.relname
                   FROM pg_constraint c
                   JOIN pg_class t ON t.oid = c.conrelid
                   JOIN pg_class r ON r.oid = c.confrelid
                  WHERE c.contype = 'f'");
foreach ($q as $r) { if ($r[0] !== $r[1]) $parents[$r[0]][$r[1]] = true; }

$tables = $my->query("SHOW TABLES")->fetchAll(PDO::FETCH_COLUMN);
sort($tables);

$ordered = array();
$placed = array();
$left = $tables;
while ($left) {
    $tier = array();
    foreach ($left as $t) {
        $ready = true;
        foreach (array_keys(isset($parents[strtolower($t)])
                            ? $parents[strtolower($t)] : array()) as $p) {
            if (!isset($placed[$p])) { $ready = false; break; }
        }
        if ($ready) $tier[] = $t;
    }
    if (!$tier) {
        fwrite(STDERR, "foreign keys form a cycle over: "
                       . implode(", ", $left) . "\n");
        exit(1);
    }
    foreach ($tier as $t) { $ordered[] = $t; $placed[strtolower($t)] = true; }
    $left = array_values(array_diff($left, $tier));
}
$tables = $ordered;

$missing = array();
foreach ($tables as $t) { if (!isset($pgcols[strtolower($t)])) $missing[] = $t; }
if ($missing) {
    fwrite(STDERR, "no PostgreSQL table for: " . implode(", ", $missing) . "\n");
    exit(1);
}

echo "-- WebGoatPHP / jFramework -- data for the pdo_pgsql adapter.\n";
echo "--\n";
echo "-- The counterpart of pdo_pgsql.schema.sql: the file\n";
echo "-- jf\\BaseDatabase::GetSQL('data') derives from the adapter class name,\n";
echo "-- with PREFIX_ standing in for the configured table prefix.\n";
echo "--\n";
echo "-- Applied whole, top to bottom, on a database the schema file has\n";
echo "-- already built. It empties every table it is about to fill, so applying\n";
echo "-- it twice leaves the same rows once rather than twice.\n";
echo "--\n";
echo "-- Generated from the live MySQL database by the migration; regenerate it\n";
echo "-- rather than editing it by hand.\n\n";
echo "SET client_encoding = 'UTF8';\n";
echo "SET standard_conforming_strings = on;\n\n";

$names = array();
foreach ($tables as $t) $names[] = pgname($t, $PREFIX);
echo "TRUNCATE TABLE\n    " . implode(",\n    ", $names) . "\n  CASCADE;\n";

$total = 0;
foreach ($tables as $t) {
    $lt = strtolower($t);
    $target = pgname($t, $PREFIX);

    // MySQL's own column order, mapped onto the names PostgreSQL folded them to.
    $cols = array();
    foreach ($my->query("SHOW COLUMNS FROM `$t`") as $c) {
        $name = $c["Field"];
        $folded = strtolower($name);
        if (!array_key_exists($folded, $pgcols[$lt])) {
            fwrite(STDERR, "no PostgreSQL column for $t.$name\n");
            exit(1);
        }
        $cols[] = array($name, $folded, $pgcols[$lt][$folded]);
    }

    $rows = $my->query("SELECT * FROM `$t`")->fetchAll(PDO::FETCH_ASSOC);
    echo "\n-- $t: " . count($rows) . " row" . (count($rows) == 1 ? "" : "s") . "\n";
    if (!$rows) continue;
    $total += count($rows);

    $collist = array();
    foreach ($cols as $c) $collist[] = $c[1];
    $head = "INSERT INTO $target (" . implode(", ", $collist) . ") VALUES\n";

    $n = 0;
    foreach ($rows as $row) {
        if ($n % $BATCH == 0) echo ($n ? ";\n" : "") . $head; else echo ",\n";
        $vals = array();
        foreach ($cols as $c) $vals[] = lit($row[$c[0]], $c[2]);
        echo "    (" . implode(", ", $vals) . ")";
        $n++;
    }
    echo ";\n";
}

// --- where the auto-numbered keys carry on from ------------------------------
echo "\n-- The next value each auto-numbered key hands out, which is state the\n";
echo "-- rows themselves do not carry: a sequence left at 1 makes the\n";
echo "-- application's first insert collide with row 1.\n";
foreach ($tables as $t) {
    $lt = strtolower($t);
    if (empty($ident[$lt])) continue;
    $target = pgname($t, $PREFIX);
    foreach ($ident[$lt] as $col) {
        $mycol = null;
        foreach ($my->query("SHOW COLUMNS FROM `$t`") as $c) {
            if (strtolower($c["Field"]) === $col) $mycol = $c["Field"];
        }
        $max = $my->query("SELECT COALESCE(MAX(`$mycol`), 0) FROM `$t`")->fetchColumn();
        echo "ALTER TABLE $target ALTER COLUMN $col RESTART WITH " . ($max + 1) . ";\n";
    }
}
fwrite(STDERR, "generated " . count($tables) . " tables, $total rows\n");
GENPHP
chmod 644 "$SCRATCH/gen.php"

install -d -m 755 "$(dirname "$DATA")"
psu "php '$SCRATCH/gen.php'" > "$DATA"
chmod 644 "$DATA"
echo "   wrote $DATA ($(wc -c < "$DATA") bytes)"

# -------------------------------------------------------------- 4. apply it --
# The framework's own substitution, and then psql rather than
# BaseDatabase::InitializeData(), which splits the file on ";" -- 4121 of these
# values contain a semicolon, so that splitter would cut statements in half.
# The file is written to be applied whole; ON_ERROR_STOP=1 makes sure it was.
sed "s/PREFIX_/$PREFIX/g" "$DATA" > "$SCRATCH/data.installed.sql"
chmod 644 "$SCRATCH/data.installed.sql"
psql_ "-q -f '$SCRATCH/data.installed.sql'"
echo "   applied $DATA"

# ------------------------------------------------------------- 6. check it --
# Assert the outcome, not the exit status of the loader: a migration that
# silently loaded nothing is the failure mode worth the next fifty lines.
# `pgloader` on this database exits 0 having silently dropped all 425 rows of
# jf_xuser, so nothing here reads an exit status as evidence that data moved.
#
# This reconciliation is scratch and is deliberately not shipped: the framework
# has no notion of one, so a file at install/_db/ that it never calls would be
# an invention sitting next to two files it does call. It exists to check this
# script's own work, per table, which is what the load needs and what a
# whole-database checksum would not give.
cat > "$SCRATCH/reconcile.php" <<'RECONCILEPHP'
<?php
$MYSOCK  = "/var/run/mysqld/mysqld.sock";
$DB      = "webgoatphp";
$SKIP    = array("jf_session");

function fail($msg) { fwrite(STDERR, "verify: $msg\n"); exit(2); }

try {
    $my = new PDO("mysql:unix_socket=$MYSOCK;dbname=$DB;charset=utf8mb4",
                  "root", "");
    $my->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch (Exception $e) { fail("MySQL: " . $e->getMessage()); }

try {
    $pg = new PDO("pgsql:host=/var/run/postgresql;port=5432;dbname=$DB;user=postgres");
    $pg->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch (Exception $e) { fail("PostgreSQL: " . $e->getMessage()); }

// PostgreSQL folds an unquoted identifier to lower case, so the four
// mixed-case table names and the mixed-case column names are looked up by
// their folded form rather than assumed.
$pgtype = array();
$q = $pg->query("SELECT c.relname, a.attname, format_type(a.atttypid, a.atttypmod)
                   FROM pg_class c
                   JOIN pg_namespace n ON n.oid = c.relnamespace
                   JOIN pg_attribute a ON a.attrelid = c.oid AND a.attnum > 0
                                      AND NOT a.attisdropped
                  WHERE c.relkind = 'r'
                    AND n.nspname NOT IN ('pg_catalog','information_schema')");
foreach ($q as $r) $pgtype[$r[0]][$r[1]] = $r[2];

function canon($v, $class) {
    if ($v === null) return "~";
    if (is_resource($v)) $v = stream_get_contents($v);
    switch ($class) {
        case "int":   return "i" . (string)(int)$v;
        case "float": return "f" . (string)(float)$v;
        case "char":  return "s" . bin2hex(rtrim($v, " "));   // MySQL CHAR does
                                                              // not return its
                                                              // padding
        case "hex":   return "s" . strtolower(ltrim($v, "\\x"));
        default:      return "s" . bin2hex($v);
    }
}

// (count, checksum) over the sorted per-row digests, so row order is not a
// difference and a changed value is.
function digest($rows, $classes) {
    $hashes = array();
    foreach ($rows as $row) {
        $parts = array();
        foreach ($classes as $col => $class) $parts[] = canon($row[$col], $class);
        $hashes[] = md5(implode("\x1f", $parts));
    }
    sort($hashes);
    return array(count($hashes), md5(implode("", $hashes)));
}

$tables = $my->query("SHOW TABLES")->fetchAll(PDO::FETCH_COLUMN);
sort($tables);

$bad = 0;
$checked = 0;
foreach ($tables as $t) {
    if (in_array($t, $SKIP)) { printf("skip     %-40s (runtime state)\n", $t); continue; }
    $lt = strtolower($t);
    if (!isset($pgtype[$lt])) {
        printf("MISSING  %-40s no such table in PostgreSQL\n", $t);
        $bad++; continue;
    }

    // MySQL's columns are the contract; PostgreSQL is checked against them.
    $mysel = array(); $pgsel = array(); $myclass = array(); $pgclass = array();
    $ok = true;
    foreach ($my->query("SHOW COLUMNS FROM `$t`") as $c) {
        $name = $c["Field"]; $folded = strtolower($name);
        if (!isset($pgtype[$lt][$folded])) {
            printf("MISSING  %-40s no column %s in PostgreSQL\n", $t, $name);
            $ok = false; break;
        }
        $mytype = strtolower(preg_replace('/\(.*/', '', $c["Type"]));
        $pt     = $pgtype[$lt][$folded];

        if (in_array($mytype, array("tinyint","smallint","mediumint","int",
                                    "integer","bigint"))) {
            $class = "int";
        } elseif (in_array($mytype, array("float","double","decimal"))) {
            $class = "float";
        } else {
            $class = "text";
        }
        $myclass[$name] = $class;

        // The two places PostgreSQL hands back something MySQL would not.
        if (strpos($pt, "bytea") === 0)          $pgclass[$name] = "hex";
        elseif (strpos($pt, "character(") === 0) $pgclass[$name] = "char";
        else                                     $pgclass[$name] = $class;

        $mysel[] = "`$name`";
        $pgsel[] = ($pgclass[$name] === "hex")
            ? "encode(\"$folded\", 'hex') AS \"$folded\""
            : "\"$folded\"";
    }
    if (!$ok) { $bad++; continue; }

    $mrows = $my->query("SELECT " . implode(", ", $mysel) . " FROM `$t`")
                ->fetchAll(PDO::FETCH_ASSOC);
    try {
        $prows = $pg->query("SELECT " . implode(", ", $pgsel) . " FROM \"$lt\"")
                    ->fetchAll(PDO::FETCH_ASSOC);
    } catch (Exception $e) {
        printf("ERROR    %-40s %s\n", $t, trim(explode("\n", $e->getMessage())[0]));
        $bad++; continue;
    }

    // Both sides are keyed by MySQL's spelling before they are digested.
    $prekeyed = array();
    foreach ($prows as $row) {
        $out = array();
        foreach ($myclass as $name => $_) $out[$name] = $row[strtolower($name)];
        $prekeyed[] = $out;
    }

    list($mn, $mh) = digest($mrows, $myclass);
    list($pn, $ph) = digest($prekeyed, $pgclass);
    $checked++;

    if ($mn === $pn && $mh === $ph) {
        printf("ok       %-40s rows=%-6d checksum=%s\n", $t, $mn, substr($mh, 0, 16));
    } else {
        $bad++;
        if ($mn !== $pn) {
            printf("MISMATCH %-40s rows: mysql=%d postgresql=%d (%+d)\n",
                   $t, $mn, $pn, $pn - $mn);
        } else {
            printf("MISMATCH %-40s rows=%d on both sides, checksum: mysql=%s postgresql=%s\n",
                   $t, $mn, substr($mh, 0, 16), substr($ph, 0, 16));
        }
    }
}

printf("\n%d table(s) compared, %d disagree\n", $checked, $bad);
exit($bad === 0 ? 0 : 1);
RECONCILEPHP
chmod 644 "$SCRATCH/reconcile.php"
psu "php '$SCRATCH/reconcile.php'"
echo "   the two databases reconcile, per table"

# The reconciliation deliberately says nothing about the sequences: they are
# not rows, so no comparison of rows can see them. Checked here directly, with
# the reading that RESTART WITH actually produces -- it sets last_value with
# is_called false and leaves pg_sequence.seqstart alone, so a check that reads
# seqstart sees 1 on a sequence that is correctly positioned.
cat > "$SCRATCH/seq.sql" <<'PGSQL'
DO $$
DECLARE r record; hi bigint; nxt bigint; bad int := 0; n int := 0;
BEGIN
    FOR r IN
        SELECT tn.nspname AS tsch, t.relname AS tbl, a.attname AS col,
               sn.nspname AS ssch, s.relname AS seq, q.seqincrement AS incr
          FROM pg_class s
          JOIN pg_namespace sn ON sn.oid = s.relnamespace
          JOIN pg_sequence q ON q.seqrelid = s.oid
          JOIN pg_depend d ON d.objid = s.oid
                          AND d.classid = 'pg_class'::regclass
                          AND d.refclassid = 'pg_class'::regclass
                          AND d.refobjsubid > 0
          JOIN pg_class t ON t.oid = d.refobjid
          JOIN pg_namespace tn ON tn.oid = t.relnamespace
          JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = d.refobjsubid
         WHERE s.relkind = 'S'
         ORDER BY 2, 3
    LOOP
        EXECUTE format('SELECT coalesce(max(%I), 0) FROM %I.%I',
                       r.col, r.tsch, r.tbl) INTO hi;
        EXECUTE format('SELECT last_value + CASE WHEN is_called THEN %s'
                       ' ELSE 0 END FROM %I.%I', r.incr, r.ssch, r.seq) INTO nxt;
        n := n + 1;
        IF nxt <= hi THEN
            RAISE WARNING 'sequence for %.% hands out % but the rows reach %',
                          r.tbl, r.col, nxt, hi;
            bad := bad + 1;
        END IF;
    END LOOP;
    IF bad > 0 THEN
        RAISE EXCEPTION '% of % identity column(s) would collide on the first'
                        ' insert', bad, n;
    END IF;
    RAISE NOTICE 'all % identity sequences are past their rows', n;
END $$;
PGSQL
chmod 644 "$SCRATCH/seq.sql"
psql_ "-q -f '$SCRATCH/seq.sql'"

# Two totals, so a silent partial load cannot pass. 27 tables, and the row
# counts MySQL itself reports.
PGROWS=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace \
      WHERE c.relkind='r' AND n.nspname NOT IN ('pg_catalog','information_schema')\"")
[ "$PGROWS" = 27 ] || { echo "expected 27 tables, found $PGROWS" >&2; exit 1; }

for pair in jf_stats:3003 jf_xuser:425 jf_users:432 contest_submissions:1500 \
            jf_rbac_userroles:1538 lesson_XSS2_messages:47 jf_options:371; do
    t=${pair%%:*}; want=${pair##*:}
    got=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
        'SELECT count(*) FROM \"$(echo "$t" | tr 'A-Z' 'a-z')\"'")
    [ "$got" = "$want" ] || { echo "$t: expected $want rows, found $got" >&2; exit 1; }
done
echo "   27 tables, spot-checked row counts agree"

# The NUL byte survived. It is the single value that forced the bytea column,
# so it is the single value worth naming in an assertion.
NULS=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc \
    \"SELECT count(*) FROM jf_options WHERE position('\\\\x00'::bytea in value) > 0\"")
[ "$NULS" -ge 1 ] || { echo "the NUL-bearing jf_options row did not survive" >&2; exit 1; }
echo "   the NUL-bearing value survived ($NULS row)"

# --------------------------------------------- 7. install it somewhere else --
# The point of PREFIX_. GetSQL() substitutes it on every install, so the pair
# of files has to build and fill a deployment configured with some other
# prefix -- which is also the one reading that can tell a data *file* from a
# dump of this particular database. Done on a scratch database that is dropped
# again; the live one is not touched.
FLIPDB=wg_solve_prefix
FLIPPFX=zz_
psu "psql -p $PGPORT -h $PGSOCK -d postgres -q -v ON_ERROR_STOP=1 \
     -c 'DROP DATABASE IF EXISTS $FLIPDB WITH (FORCE)'"
psu "psql -p $PGPORT -h $PGSOCK -d postgres -q -v ON_ERROR_STOP=1 \
     -c \"CREATE DATABASE $FLIPDB TEMPLATE template0 ENCODING 'UTF8' \
           LC_COLLATE 'C' LC_CTYPE 'C'\""

sed "s/PREFIX_/$FLIPPFX/g" "$SCHEMA" > "$SCRATCH/schema.flip.sql"
sed "s/PREFIX_/$FLIPPFX/g" "$DATA"   > "$SCRATCH/data.flip.sql"
chmod 644 "$SCRATCH/schema.flip.sql" "$SCRATCH/data.flip.sql"
psu "psql -p $PGPORT -h $PGSOCK -d $FLIPDB -q -v ON_ERROR_STOP=1 -f '$SCRATCH/schema.flip.sql'"
psu "psql -p $PGPORT -h $PGSOCK -d $FLIPDB -q -v ON_ERROR_STOP=1 -f '$SCRATCH/data.flip.sql'"

for pair in ${FLIPPFX}stats:3003 ${FLIPPFX}xuser:425 ${FLIPPFX}users:432 \
            ${FLIPPFX}options:371 contest_submissions:1500 lesson_xss2_messages:47; do
    t=${pair%%:*}; want=${pair##*:}
    got=$(psu "psql -p $PGPORT -h $PGSOCK -d $FLIPDB -Atqc 'SELECT count(*) FROM \"$t\"'")
    [ "$got" = "$want" ] || {
        echo "$FLIPPFX install: $t expected $want rows, found $got" >&2; exit 1; }
done

# On a fresh install nothing but the data file can have positioned the
# sequences, so this is the reading that separates a file that carries the
# AUTO_INCREMENT state from one that only carries rows.
cat > "$SCRATCH/flipseq.sql" <<PGSQL
SELECT s.relname
  FROM pg_class s
  JOIN pg_depend d ON d.objid = s.oid
                  AND d.classid = 'pg_class'::regclass
                  AND d.refclassid = 'pg_class'::regclass
                  AND d.refobjsubid > 0
  JOIN pg_class t ON t.oid = d.refobjid
  JOIN pg_attribute a ON a.attrelid = t.oid AND a.attnum = d.refobjsubid
 WHERE s.relkind = 'S' AND t.relname = '${FLIPPFX}stats' AND a.attname = 'id';
PGSQL
chmod 644 "$SCRATCH/flipseq.sql"
FLIPSEQ=$(psu "psql -p $PGPORT -h $PGSOCK -d $FLIPDB -Atq -f '$SCRATCH/flipseq.sql'")
[ -n "$FLIPSEQ" ] || {
    echo "$FLIPPFX install: ${FLIPPFX}stats.id is on no sequence" >&2; exit 1; }
FLIPNEXT=$(psu "psql -p $PGPORT -h $PGSOCK -d $FLIPDB -Atqc \
    \"SELECT last_value + CASE WHEN is_called THEN 1 ELSE 0 END FROM \\\"$FLIPSEQ\\\"\"")
[ "$FLIPNEXT" -gt 3003 ] || {
    echo "$FLIPPFX install: $FLIPSEQ hands out $FLIPNEXT, which is not past" \
         "the 3003 rows just loaded" >&2; exit 1; }

psu "psql -p $PGPORT -h $PGSOCK -d postgres -q -c 'DROP DATABASE IF EXISTS $FLIPDB WITH (FORCE)'"
echo "   both files install cleanly at the $FLIPPFX prefix, sequences included"

# Steps 1 and 2 are still in place. Cheap to assert, and both are scored here.
grep -q 'STRICT_TRANS_TABLES' /etc/mysql/my.cnf \
    || { echo "step 1's sql_mode is gone from /etc/mysql/my.cnf" >&2; exit 1; }
[ -f "$SCHEMA" ] || { echo "step 2's schema file is gone" >&2; exit 1; }

/usr/local/sbin/wg-services status || true
echo "== step 03 done"
