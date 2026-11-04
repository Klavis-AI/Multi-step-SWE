set -u
S="$SQLITE3"
# a single SQL argument after the file name is executed, then the program exits
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'p');" || exit 1
echo "created=$( [ -f main.db ] && echo yes || echo no )"
# with an SQL argument, standard input is not read
printf 'INSERT INTO t VALUES(99,"ignored");\n' | $S -init /dev/null -batch main.db "SELECT b FROM t ORDER BY a;"
# with no SQL argument, statements come from standard input until end of file
printf 'INSERT INTO t VALUES(2,%s);\nSELECT a, b FROM t ORDER BY a;\n' "'q'" | $S -init /dev/null -batch main.db
# a transient in-memory database leaves no file behind
$S -init /dev/null -batch :memory: "CREATE TABLE m(x); INSERT INTO m VALUES(5); SELECT x FROM m;"
echo "memfile=$( [ -e ':memory:' ] && echo yes || echo no )"
