# R10.3: ADD COLUMN without a DEFAULT leaves the rows already on disk reading
# NULL in the new column, a later INSERT fills it, and two adds in a row pin
# the column order pragma_table_info reports.
#
# ADD COLUMN without a DEFAULT leaves existing rows reading NULL in the new
# column, and the column has to be usable in a predicate straight away.
# Adding two columns in a row pins their order in the record as well.
#
# This is milestone 10's gate case. An earlier round read that as a reason to
# leave the `pragma_table_info` select without an `ORDER BY cid` while adding
# one to every other case in the step. That was backwards. A gate earns nothing
# and can only veto, so an order no document requires does the MOST damage
# here: a conforming solution that returned the pragma's rows in another order
# would score zero for the whole step at milestones 11 through 15. `pragma.html`
# promises one row per column and says nothing about their order, and
# `pragma_table_info` in this form is a table-valued function, so nothing binds
# the order without a top-level `ORDER BY`.
#
# `ORDER BY cid` was therefore added. The expected output does not change --
# the rows were already in cid order -- and determinism was re-measured at
# N=40 at steps 10, 11 and 15. Do not remove it.
#
# For the same reason there is no cross-read of stock sqlite3 here, and that is
# a decision rather than an oversight. Stock has a perfectly good opinion about
# ADD COLUMN's NULL fill -- it is ordinary SQLite -- but adding a second process
# to a gate case voids the determinism proof that licenses it as a gate, across
# every step it gates, and buys nothing: a gate earns no score and can only
# veto. The second opinion is carried by milestone 10's own scored siblings --
# the step where this case is scored in the `s10-alter-table-columns` group
# beside them, and they carry the cross-read -- and not by anything that runs
# here: they are not part of the inherited gate.
# `add-column-with-default.sh` replays ADD COLUMN, the short-record read-back
# and `pragma_table_info` against stock on its own file;
# `add-column-not-null-accepted.sh` hands stock the file this engine wrote and
# reads the added column's shape and rows back out of it; and
# `drop-column-rows-and-shape.sh` does the same for the narrowing half. If this
# case and the engine were wrong together, those three would have to be wrong
# with them.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2);
ALTER TABLE t ADD COLUMN b TEXT;
ALTER TABLE t ADD COLUMN c INTEGER;
INSERT INTO t VALUES(3,'three',33);
SELECT a, typeof(b), typeof(c) FROM t ORDER BY a;
SELECT a, b, c FROM t WHERE c IS NOT NULL ORDER BY a;
SELECT count(*) FROM t WHERE b IS NULL;
SELECT cid, name FROM pragma_table_info('t') ORDER BY cid;
SELECT count(*) FROM t;
SQL
