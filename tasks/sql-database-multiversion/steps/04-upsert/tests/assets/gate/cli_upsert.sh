# Gate, boundary 1 of 2: proof of life through this step's new capability at
# the command line -- one INSERT ... ON CONFLICT (target) DO UPDATE SET that
# updates the row it collides with, leaves the other rows of the table alone,
# and inserts a row that collides with nothing (TEST_INTERFACE.md 7).
set -u
S="$SQLITE3"
$S -init /dev/null -batch gate.db \
   "CREATE TABLE g(k INTEGER PRIMARY KEY, c TEXT);
    INSERT INTO g VALUES(1,'one'),(2,'two');" || exit 1
$S -init /dev/null -batch gate.db \
   "INSERT INTO g(k,c) VALUES(2,'TWO'),(3,'three')
      ON CONFLICT(k) DO UPDATE SET c=excluded.c;" || exit 1
$S -init /dev/null -batch gate.db "SELECT k,c FROM g ORDER BY k;" || exit 1
