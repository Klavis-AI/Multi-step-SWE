# R09.1: a bare VALUES statement, run on its own and alongside ordinary SELECTs.
# A bare VALUES statement returns its rows in the written order: no
# deduplication, no sorting, one result line per row, and it coexists with
# ordinary SELECTs in the same script.
#
# At v0.0.19 and v0.0.20 the statement reaches the translator and panics.
# The last line is the differential: a .sh case is not covered by the
# generator's automatic sqlite3 comparison, so the case makes that comparison
# itself -- over the same script, statement for statement, rather than over a
# statement resembling it.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE ref(a INTEGER, b TEXT);
INSERT INTO ref VALUES(11,22);
SQL

cat > script.sql <<'SQL'
VALUES(11,22),(33,44),(11,22);
VALUES(55);
VALUES('a','b','c');
VALUES(NULL,1);
SELECT 'after:'||count(*) FROM ref;
SQL

"$CLI" -m "$CLI_MODE" -q main.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/' > engine.txt
"$SQLITE3" -batch -list -separator '|' -noheader main.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*$/d' > stock.txt

cat engine.txt
if cmp -s engine.txt stock.txt; then
  echo 'sqlite3-agrees:yes'
else
  echo 'sqlite3-agrees:no'
fi
