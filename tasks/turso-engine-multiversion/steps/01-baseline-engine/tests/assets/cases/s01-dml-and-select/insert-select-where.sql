-- R01.2: INSERT and SELECT with WHERE and ORDER BY, row for row against stock
-- sqlite3.
--
-- R01.2 is the row-level agreement requirement, and the whole of what it is
-- scored on at this step is INSERT and SELECT -- `UPDATE` is a parse error at
-- this checkpoint and `DELETE` does not delete, as the step's scope note and
-- `TEST_INTERFACE.md` both say. This is its simplest shape: a multi-row VALUES
-- insert, a filtered ordered projection, and the count over the table. Being a
-- `.sql` case, the matrix replays it against stock sqlite3 automatically, so
-- the agreement R01.2 asks for is checked rather than asserted.
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two'),(3,'three');
SELECT a, b FROM t WHERE a >= 2 ORDER BY a;
SELECT count(*) FROM t;
