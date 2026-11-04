# Gate, boundary 1 of 2: proof of life through this step's new capability at
# the command line -- json_extract() returns the value PROFILE.md 3.1 says it
# holds, and an index on an expression can be created and then queried.
set -u
S="$SQLITE3"
$S -init /dev/null -batch gate.db \
   "CREATE TABLE g(id INTEGER PRIMARY KEY, body TEXT);
    INSERT INTO g VALUES(1,'{\"a\":42}'),(2,'{\"a\":7}');" || exit 1
# json_extract(), json1.html 3.4
$S -init /dev/null -batch gate.db "SELECT json_extract('{\"a\":42}','\$.a');" || exit 1
$S -init /dev/null -batch gate.db "SELECT id FROM g WHERE json_extract(body,'\$.a')=7;" || exit 1
# an index on an expression, expridx.html
$S -init /dev/null -batch gate.db "CREATE INDEX g_a ON g(json_extract(body,'\$.a'));" || exit 1
$S -init /dev/null -batch gate.db "SELECT id FROM g ORDER BY json_extract(body,'\$.a');" || exit 1
