# Gate, boundary 1 of 2: proof of life through this step's new capability at
# the command line, and exactly what TEST_INTERFACE.md 7 says the gate is --
# one row_number() OVER (ORDER BY ...) that numbers the rows in the order the
# window's ORDER BY gives them while the output order is fixed by the
# statement's own ORDER BY, and one ALTER TABLE ... RENAME COLUMN whose new
# column name a following query can use.
#
# PROFILE.md 5.1: a function invocation with an OVER clause is a window
# function, and a window function may appear in the result set of a SELECT.
# 5.4: row_number() is "The number of the row within the current partition.
# Rows are numbered starting from 1 in the order defined by the ORDER BY clause
# in the window definition", so with ORDER BY v over v = 30,10,20 the rows
# k = 1,2,3 get 3,1,2.  v is UNIQUE, so the window ORDER BY totally orders the
# one partition and there are no peers.
# 5.2: "this does not affect the order in which results are returned from the
# overall query.  The order of the final output is still governed by the ORDER
# BY clause attached to the SELECT statement" -- here ORDER BY k.
# 5.9: ALTER TABLE t RENAME COLUMN old TO new changes the column's name, the
# new name works in a following SELECT, and no value changes.
set -u
S="$SQLITE3"
$S -init /dev/null -batch gate.db \
   "CREATE TABLE g(k INTEGER PRIMARY KEY, v INTEGER UNIQUE);
    INSERT INTO g VALUES(1,30),(2,10),(3,20);" || exit 1
$S -init /dev/null -batch gate.db \
   "SELECT k,
           row_number() OVER (ORDER BY v) AS rn
      FROM g ORDER BY k;" || exit 1
$S -init /dev/null -batch gate.db \
   "ALTER TABLE g RENAME COLUMN v TO amount;" || exit 1
$S -init /dev/null -batch gate.db \
   "SELECT k, amount FROM g ORDER BY k;" || exit 1
