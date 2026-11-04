# Gate, boundary 1 of 2: the command-line program answers a WITH query and a
# VALUES query with the rows PROFILE.md sections 2.1 and 2.2 say they hold.
set -u
S="$SQLITE3"
$S -init /dev/null -batch gate.db \
   "CREATE TABLE g(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO g VALUES(1,'ok'),(2,'two');" || exit 1
# an ordinary common table expression on SELECT
$S -init /dev/null -batch gate.db \
   "WITH c(k,v) AS (SELECT a, b FROM g) SELECT k, v FROM c ORDER BY k;" || exit 1
# a VALUES clause as a complete statement
$S -init /dev/null -batch gate.db "VALUES(3,'three');" || exit 1
# a VALUES clause as a subquery in FROM
$S -init /dev/null -batch gate.db "SELECT * FROM (VALUES(4,'four'),(5,'five')) ORDER BY 1;" || exit 1
