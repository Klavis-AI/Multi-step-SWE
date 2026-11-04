# R07.12: count(), sum(), group_concat(), min() and max() computed over a range
# of an index see exactly the rows the range describes.
#
# Before v0.0.19 the engine starts the scan at the wrong index entry and folds
# one extra row into every aggregate, which is invisible in a plain row listing
# that happens to be sorted afterwards but changes every total.
#
# Stock sqlite3 builds the index, the engine only reads it, and stock sqlite3
# then computes the same seven aggregates over the same file, so each engine
# line has a reference line beside it rather than only a frozen constant.
#
# group_concat() has no defined element order, so the one call that uses it
# takes its rows from a subquery with an explicit ORDER BY. Without that the
# expected string would be pinning whichever order this engine happens to
# emit, which is not something any document the candidate receives fixes.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE u(k TEXT, v INTEGER);
INSERT INTO u VALUES('ada',1),('bob',2),('cyd',3),('dan',4),('eve',5);
CREATE INDEX i_kv ON u(k,v);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'count-gt:'||count(*) FROM u WHERE k>'ada';
SELECT 'count-two-sided:'||count(*) FROM u WHERE k>'ada' AND v<5;
SELECT 'sum-gt:'||sum(v) FROM u WHERE k>'ada';
SELECT 'concat-gt:'||group_concat(v) FROM (SELECT v FROM u WHERE k>'ada' ORDER BY v);
SELECT 'min-gt:'||min(v) FROM u WHERE k>'ada';
SELECT 'max-lt:'||max(v) FROM u WHERE k<'eve';
SELECT 'count-all:'||count(*) FROM u;
SQL

"$SQLITE3" -batch -list -noheader main.db 2>&1 <<'SQL' | sed 's/^/sqlite:/'
SELECT 'count-gt:'||count(*) FROM u WHERE k>'ada';
SELECT 'count-two-sided:'||count(*) FROM u WHERE k>'ada' AND v<5;
SELECT 'sum-gt:'||sum(v) FROM u WHERE k>'ada';
SELECT 'concat-gt:'||group_concat(v) FROM (SELECT v FROM u WHERE k>'ada' ORDER BY v);
SELECT 'min-gt:'||min(v) FROM u WHERE k>'ada';
SELECT 'max-lt:'||max(v) FROM u WHERE k<'eve';
SELECT 'count-all:'||count(*) FROM u;
SQL
