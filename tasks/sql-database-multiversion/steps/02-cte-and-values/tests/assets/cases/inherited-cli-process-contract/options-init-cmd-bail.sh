set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'p'),(2,NULL);" || exit 1
# -init FILE runs the start-up commands in FILE instead of any default file
printf '.headers on\n.separator ,\n' > startup.txt
$S -init startup.txt -batch main.db "SELECT a AS col_a, b AS col_b FROM t ORDER BY a;"
# -init /dev/null suppresses them again
$S -init /dev/null -batch main.db "SELECT a AS col_a, b AS col_b FROM t ORDER BY a;"
# -cmd COMMAND runs one dot command before the input is processed
$S -init /dev/null -batch -cmd ".nullvalue <null>" main.db "SELECT b FROM t ORDER BY a;"
$S -init /dev/null -batch -cmd ".headers on" main.db "SELECT a AS col_a FROM t ORDER BY a;"
