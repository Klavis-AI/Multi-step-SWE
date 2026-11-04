# R07.13: an index B-tree stock sqlite3 built is read by the engine, and stock
# sqlite3 then re-opens the same file, answers PRAGMA integrity_check with ok,
# still finds the index row and answers the same query identically.
#
# Cross-implementation agreement on an index B-tree, in the direction this
# checkpoint supports: stock sqlite3 builds the index, the engine reads through
# it, and stock sqlite3 then re-opens the same file and confirms both that it
# is still structurally sound and that the index still answers the query the
# same way the engine just did.
#
# The engine's invocation ends with a count over the table it touched, the
# durability barrier this package requires of every case that hands a file
# between processes.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE u(k TEXT, v INTEGER);
INSERT INTO u VALUES('ada',1),('bob',2),('cyd',3),('dan',4),('eve',5);
CREATE INDEX i_kv ON u(k,v);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'engine:'||k||'='||v FROM u WHERE k>'bob' ORDER BY k,v;
SELECT 'engine-count:'||count(*) FROM u WHERE k>'ada';
SELECT 'engine-barrier:'||count(*) FROM u;
SQL

echo "sqlite-integrity:$("$SQLITE3" -batch -list -noheader main.db 'PRAGMA integrity_check;')"
"$SQLITE3" -batch -list -noheader main.db <<'SQL' | sed 's/^/sqlite:/'
SELECT 'row:'||k||'='||v FROM u WHERE k>'bob' ORDER BY k,v;
SELECT 'count:'||count(*) FROM u WHERE k>'ada';
SELECT 'indexes:'||count(*) FROM sqlite_schema WHERE type='index';
SQL
