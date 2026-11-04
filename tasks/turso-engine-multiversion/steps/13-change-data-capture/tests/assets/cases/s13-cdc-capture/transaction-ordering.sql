-- R13.4: the order and the numbering of the capture rows that one transaction
-- produces.
-- Five mutations are issued inside BEGIN ... COMMIT in a deliberately
-- non-key order; after the COMMIT the capture rows read back in mutation
-- order, numbered 1 to 5 with no gap, each carrying its own change_type and
-- the rowid it touched. The ids are allocated by the capture table's own
-- primary key.
-- What the COMMIT settles is the set, not the moment of first visibility:
-- after it, all five of this transaction's capture rows are there and no
-- others are, which is what `SELECT count(*)` on the line after the ordering
-- query asserts. Reading the capture table part way through the transaction
-- would show the rows already written, because a connection sees its own
-- uncommitted work; this case never does that, and nothing here grades it.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
BEGIN;
INSERT INTO t VALUES(3,'c');
INSERT INTO t VALUES(1,'a');
UPDATE t SET v='A' WHERE id=1;
INSERT INTO t VALUES(2,'b');
DELETE FROM t WHERE id=3;
COMMIT;
SELECT change_id, change_type, id FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
