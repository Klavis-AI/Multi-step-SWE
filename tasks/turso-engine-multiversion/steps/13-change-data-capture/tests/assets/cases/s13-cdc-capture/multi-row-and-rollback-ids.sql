-- R13.1, R13.2, R13.3, R13.4, R13.5: one capture row per *row* a statement
-- mutates, and the numbering across a rollback.
--
-- Every other mutation case at this step changes one row at a time, so an
-- engine that appended one capture row per *statement* would pass them all.
-- Here a multi-row INSERT, a multi-row UPDATE ... WHERE and a multi-row
-- DELETE ... WHERE each touch more than one row, and the capture table must
-- hold one row per mutated row: three rows from the INSERT, two from the
-- UPDATE, one from the DELETE and one from the insert that closes the batch,
-- seven in all.
--
-- The first query reads the capture rows sorted by change_type and then by the
-- affected rowid, which orders them without depending on anything the
-- documents leave free: which capture row a multi-row statement writes first
-- is not stated anywhere, so the rows are read by what they say about the
-- table rather than by the order they were written in. The second query reads
-- the ids themselves, sorted, and is what asserts the numbering: seven rows
-- numbered 1 to 7 with no gap and no repeat. A transaction that inserted two
-- more rows and was rolled back sits between the DELETE and the last INSERT,
-- so its discarded rows must have consumed none of those numbers -- an engine
-- numbering from a counter that survived the rollback would leave a gap here.
--
-- The image bytes follow TEST_INTERFACE.md's worked examples: the
-- INTEGER PRIMARY KEY column carries its own value (serial type 09 for the
-- value 1, 01 plus the byte for 2, 3 and 6) and a one-character text column is
-- serial type 0F.
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
PRAGMA unstable_capture_data_changes_conn('full');
INSERT INTO t VALUES(1,'a'),(2,'b'),(3,'c');
UPDATE t SET v='z' WHERE id <= 2;
DELETE FROM t WHERE id = 3;
BEGIN;
INSERT INTO t VALUES(4,'d'),(5,'e');
ROLLBACK;
INSERT INTO t VALUES(6,'f');
SELECT change_type, id, hex(before), hex(after) FROM turso_cdc ORDER BY change_type, id;
SELECT change_id FROM turso_cdc ORDER BY change_id;
SELECT count(*) FROM turso_cdc;
SELECT id, v FROM t ORDER BY id;
