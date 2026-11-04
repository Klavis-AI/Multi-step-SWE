# R09.3: UNION vs UNION ALL on the same inputs, re-read through a fresh process.
# UNION removes duplicate rows where UNION ALL keeps every one, matching stock
# sqlite3 on the same inputs.
#
# Before v0.0.21 UNION returns every row. Note the second read: the engine
# reads the file back through a fresh process, so this pins the deduplication
# to the query rather than to a cached plan.
#
# THE COMPOUND BLOCK IS SORTED BEFORE IT IS COMPARED, DELIBERATELY. What R09.3
# is about is which rows a compound returns and how many times -- the multiset
# -- not the sequence they arrive in. `lang_select.html` section 4, which
# instruction.md points the agent at, says outright that a SELECT returning
# more than one row without an ORDER BY returns those rows in an undefined
# order, so grading the raw sequence would fail a conforming engine that
# deduplicates through a hash and emits first-seen order instead of through a
# sorter. There is no in-SQL way to fix the order at this checkpoint: both
# `ORDER BY` on a compound and a compound inside a FROM-clause subquery are
# refused here (`Parse error: ORDER BY is not supported for compound SELECTs
# yet`, `Parse error: Only non-compound SELECT queries are currently supported
# in FROM clause subqueries`), which is why the sort is done in the shell,
# where a .sh case owns its own canonicalisation. Every row carries a tag, so
# sorting cannot let one statement's rows drift into another's, and the same
# sort is applied to stock sqlite3's answer, so the comparison below is still
# a real second opinion on every row and on every duplicate.
set -u

# UNION, INTERSECT and SELECT DISTINCT are gated behind the opt-in index
# switch from v0.0.22 to v0.1.3, and the switch changes shape across that
# span, so ask the shell what it accepts rather than assuming. An engine with
# no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
CREATE TABLE s(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'x'),(2,'y'),(1,'x'),(3,'z');
INSERT INTO s VALUES(2,'y'),(1,'x'),(4,'w');
SQL

cat > compound.sql <<'SQL'
SELECT 'u:'||a FROM t UNION SELECT 'u:'||a FROM s;
SELECT 'ua:'||a FROM t UNION ALL SELECT 'ua:'||a FROM s;
SELECT 'uc:'||count(*) FROM t UNION SELECT 'uc:'||count(*) FROM s;
SELECT 'barrier:'||count(*) FROM t;
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db < compound.sql 2>/dev/null \
  | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/' \
  | LC_ALL=C sort > engine.txt

# Differential: a .sh case is not covered by the generator's automatic sqlite3
# replay, so the case makes the comparison itself -- over the same four
# statements, on the same database file, under the same sort.
"$SQLITE3" -batch -list -separator '|' -noheader main.db < compound.sql 2>/dev/null \
  | sed '/^[[:space:]]*$/d' \
  | LC_ALL=C sort > stock.txt

cat engine.txt
if cmp -s engine.txt stock.txt; then
  echo 'sqlite3-agrees:yes'
else
  echo 'sqlite3-agrees:no'
fi

"$CLI" -m "$CLI_MODE" $IDX -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'reread:'||count(*) FROM t;
SQL
