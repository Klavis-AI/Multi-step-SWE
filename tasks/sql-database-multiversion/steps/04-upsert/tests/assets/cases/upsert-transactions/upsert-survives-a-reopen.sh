set -u
# PROFILE.md 4.8 with step 1 section 7: a committed upsert survives closing and
# reopening the database file and is visible to a second sqlite3 process.
S="$SQLITE3"
$S -init /dev/null -batch db1 \
   "CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT, n INTEGER DEFAULT 0);
    INSERT INTO t VALUES(1,'one',1),(2,'two',2);" || exit 1
# a second process upserts over what the first wrote
$S -init /dev/null -batch db1 \
   "INSERT INTO t(k,c,n) VALUES(1,'ONE',10),(3,'three',3)
      ON CONFLICT(k) DO UPDATE SET c=excluded.c, n=t.n+excluded.n;" || exit 1
# a third process reads the result back
$S -init /dev/null -batch db1 "SELECT k,c,n FROM t ORDER BY k;" || exit 1
echo "---"
# a transaction rolled back inside one process leaves nothing for the next
$S -init /dev/null -batch db1 \
   "BEGIN;
    INSERT INTO t(k,c,n) VALUES(2,'TWO',99) ON CONFLICT(k) DO UPDATE SET n=excluded.n;
    ROLLBACK;" || exit 1
$S -init /dev/null -batch db1 "SELECT k,c,n FROM t ORDER BY k;" || exit 1
echo "---"
$S -init /dev/null -batch db1 "PRAGMA integrity_check;" || exit 1
