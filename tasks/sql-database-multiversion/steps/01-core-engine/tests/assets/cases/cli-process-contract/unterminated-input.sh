set -u
# TEST_INTERFACE.md section 2.1: statements read from standard input are
# separated by ';', and a statement still unterminated when end of file is
# reached is not executed. The exit status of a run that ends mid-statement is
# deliberately not asserted here - only the effect on the database is.
S="$SQLITE3"
printf 'CREATE TABLE t(a INTEGER PRIMARY KEY);\nINSERT INTO t VALUES(1);\nINSERT INTO t VALUES(2)' \
    | $S -init /dev/null -batch main.db > /dev/null 2>&1
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
echo "---"
# the same text with the final ';' present executes every statement
printf 'DELETE FROM t;\nINSERT INTO t VALUES(1);\nINSERT INTO t VALUES(2);\n' \
    | $S -init /dev/null -batch main.db > /dev/null 2>&1
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
