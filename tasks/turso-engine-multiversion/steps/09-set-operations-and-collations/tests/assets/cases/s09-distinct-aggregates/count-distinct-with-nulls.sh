# R09.4: DISTINCT aggregates. count(DISTINCT x) and sum(DISTINCT x) fold
# duplicates and skip NULL arguments, SELECT DISTINCT folds whole rows, and
# every answer is the one stock sqlite3 gives for the same script.
#
# At v0.0.20 the DISTINCT aggregate is unimplemented. The last line is the
# differential: a .sh case is not covered by the generator's automatic sqlite3
# comparison, so the case makes that comparison itself.
set -u

# SELECT DISTINCT and the DISTINCT aggregates are gated behind the opt-in
# index switch from v0.0.22 to v0.1.3, and the switch changes shape across
# that span, so ask the shell what it accepts rather than assuming. An engine
# with no such option gets no option.
IDX=""
case "$("$CLI" --help 2>&1)" in
  *"--experimental-indexes <"*) IDX="--experimental-indexes=true" ;;
  *"--experimental-indexes"*)   IDX="--experimental-indexes" ;;
esac

cat > script.sql <<'SQL'
CREATE TABLE d(x INTEGER, y TEXT);
INSERT INTO d VALUES(1,'a'),(2,'b'),(1,'a'),(NULL,'c'),(3,NULL),(NULL,'a');
SELECT 'rows:'||count(*) FROM d;
SELECT 'count-x:'||count(x) FROM d;
SELECT 'distinct-x:'||count(DISTINCT x) FROM d;
SELECT 'distinct-y:'||count(DISTINCT y) FROM d;
SELECT 'sum-distinct-x:'||sum(DISTINCT x) FROM d;
SELECT 'distinct-rows:'||count(*) FROM (SELECT DISTINCT x FROM d);
SELECT 'distinct-pairs:'||count(*) FROM (SELECT DISTINCT x,y FROM d);
SELECT 'distinct-nonnull:'||count(*) FROM (SELECT DISTINCT x FROM d WHERE x IS NOT NULL);
SELECT 'distinct-in-group:'||count(DISTINCT x) FROM d WHERE y='a';
SELECT 'empty:'||count(DISTINCT x) FROM d WHERE x>100;
SQL

"$CLI" -m "$CLI_MODE" $IDX -q main.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/' > engine.txt
"$SQLITE3" -batch -list -separator '|' -noheader ref.db < script.sql 2>/dev/null \
  | sed '/^[[:space:]]*$/d' > stock.txt

cat engine.txt
if cmp -s engine.txt stock.txt; then
  echo 'sqlite3-agrees:yes'
else
  echo 'sqlite3-agrees:no'
fi
