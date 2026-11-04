-- R06.11: an UPDATE under a predicate changes the matching row and leaves
-- the others, and the unassigned columns, alone.
--
-- One row changes, the others must not. The unchanged rows are read back in
-- key order so a rewrite that disturbs neighbours is visible.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
INSERT INTO t VALUES(1,10,'x'),(2,20,'y'),(3,30,'z');
UPDATE t SET b='changed' WHERE id=2;
SELECT id,a,b FROM t ORDER BY id;
SELECT count(*), sum(a) FROM t;
