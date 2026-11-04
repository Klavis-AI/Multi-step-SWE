set -u
# PROFILE.md 5.9: "the rename takes part in transactions like any other
# statement (step 1 section 7): it is undone by ROLLBACK and by ROLLBACK TO a
# savepoint, and it survives COMMIT, a reopen and a second process."
S="$SQLITE3"
$S -init /dev/null -batch db1 \
   "CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT);
    INSERT INTO t VALUES(1,'x'),(2,'y');" || exit 1
# a rename in its own process, read back by a second one
$S -init /dev/null -batch db1 "ALTER TABLE t RENAME COLUMN alpha TO beta;" || exit 1
$S -init /dev/null -batch db1 \
   "SELECT sql FROM sqlite_master WHERE name='t';
    SELECT k,beta FROM t ORDER BY k;" || exit 1
echo "---"
# ROLLBACK undoes it
$S -init /dev/null -batch db1 \
   "BEGIN; ALTER TABLE t RENAME COLUMN beta TO gamma; ROLLBACK;" || exit 1
$S -init /dev/null -batch db1 \
   "SELECT sql FROM sqlite_master WHERE name='t';
    SELECT k,beta FROM t ORDER BY k;" || exit 1
echo "---"
# so does ROLLBACK TO a savepoint
$S -init /dev/null -batch db1 \
   "SAVEPOINT s1;
    ALTER TABLE t RENAME COLUMN beta TO gamma;
    SELECT sql FROM sqlite_master WHERE name='t';
    ROLLBACK TO s1;
    RELEASE s1;
    SELECT sql FROM sqlite_master WHERE name='t';" || exit 1
echo "---"
# and COMMIT makes it durable across a reopen
$S -init /dev/null -batch db1 \
   "BEGIN; ALTER TABLE t RENAME COLUMN beta TO delta; COMMIT;" || exit 1
$S -init /dev/null -batch db1 \
   "SELECT sql FROM sqlite_master WHERE name='t';
    SELECT k,delta FROM t ORDER BY k;
    PRAGMA integrity_check;" || exit 1
