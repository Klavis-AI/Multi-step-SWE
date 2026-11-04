set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY);" ; echo "ok_rc=$?"
$S -init /dev/null -batch main.db "SELECT * FROM no_such_table;" 2> /dev/null ; \
    [ $? -ne 0 ] && echo "err_nonzero=yes" || echo "err_nonzero=no"
printf 'INSERT INTO t VALUES(1);\nINSERT INTO t VALUES(1);\nINSERT INTO t VALUES(2);\n' \
    | $S -init /dev/null -batch main.db 2> /dev/null ; \
    [ $? -ne 0 ] && echo "batch_err_nonzero=yes" || echo "batch_err_nonzero=no"
# without .bail the run continues past the failing statement
$S -init /dev/null -batch main.db "SELECT a FROM t ORDER BY a;"
# stdout carries only query results; diagnostics go to standard error
out=$($S -init /dev/null -batch main.db "SELECT 7; SELECT * FROM no_such_table;" 2> /dev/null)
echo "stdout_only=[$out]"
