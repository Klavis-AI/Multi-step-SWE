set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'p'),(2,NULL);" || exit 1
# the documented start-up defaults: list mode, headers off, "|" separator,
# empty string for NULL, .bail off
printf 'SELECT a, b FROM t ORDER BY a;\n' | $S -init /dev/null -batch main.db
# .headers on / off, .separator, .nullvalue, .mode list
printf '.headers on\n.separator ;\n.nullvalue NIL\nSELECT a AS x, b AS y FROM t ORDER BY a;\n.headers off\n.separator |\n.mode list\nSELECT a, b FROM t ORDER BY a;\n' \
    | $S -init /dev/null -batch main.db
# .bail on stops after the first error; .quit stops reading input
printf '.bail on\nSELECT 1;\nSELECT * FROM nope;\nSELECT 2;\n' | $S -init /dev/null -batch main.db 2> /dev/null
echo "bail_rc_nonzero=$( [ $? -ne 0 ] && echo yes || echo no )"
printf 'SELECT 3;\n.quit\nSELECT 4;\n' | $S -init /dev/null -batch main.db
