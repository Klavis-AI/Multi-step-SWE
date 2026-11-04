-- R13.5: a ROLLBACK discards the capture rows along with the change.
-- A capture row is written in the same transaction as the mutation that
-- produced it, so rolling that transaction back must leave no trace of
-- either. One capture row is committed first; a transaction then inserts a
-- row and updates the first, and is rolled back. Afterwards the capture table
-- holds only the row committed before the transaction, and the user table
-- holds only the value the rolled-back UPDATE did not change.
-- The sibling rollback-leaves-file-bytes.sh grades R13.5's other half, that
-- the file bytes are unchanged as well.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'kept');
BEGIN;
INSERT INTO t VALUES(2,'rolled back');
UPDATE t SET v='also rolled back' WHERE id=1;
ROLLBACK;
SELECT change_id, change_type, id FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
