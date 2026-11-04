# Gate, boundary 1 of 2: the command-line program creates a database, stores a
# row, reads it back, and the file still holds the row for a second process.
set -u
S="$SQLITE3"
$S -init /dev/null -batch gate.db \
   "CREATE TABLE g(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO g VALUES(1,'ok');" || exit 1
$S -init /dev/null -batch gate.db "SELECT a, b FROM g ORDER BY a;" || exit 1
[ -f gate.db ] || { echo "no database file"; exit 1; }
printf 'INSERT INTO g VALUES(2,%s);\nSELECT a, b FROM g ORDER BY a;\n' "'two'" \
    | $S -init /dev/null -batch gate.db || exit 1
$S -init /dev/null -batch gate.db "SELECT count(*) FROM g;" || exit 1
