-- An index on an expression is maintained by ordinary INSERT, UPDATE and
-- DELETE exactly as a column index is (PROFILE.md 3.4): the value the index
-- holds is recomputed from the row, so a query written against the indexed
-- expression must see every change.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
CREATE INDEX ta ON t(abs(a));
CREATE INDEX tb ON t(lower(b));
INSERT INTO t VALUES(1,-5,'Beta'),(2,3,'alpha');
SELECT id FROM t WHERE abs(a)=5;
SELECT id FROM t WHERE lower(b)='alpha';
INSERT INTO t VALUES(3,-9,'GAMMA');
SELECT id FROM t WHERE abs(a)=9;
SELECT id FROM t WHERE lower(b)='gamma';
UPDATE t SET a = 9 WHERE id = 1;
SELECT id FROM t WHERE abs(a)=9 ORDER BY id;
SELECT id FROM t WHERE abs(a)=5;
UPDATE t SET b = 'Delta' WHERE id = 2;
SELECT id FROM t WHERE lower(b)='delta';
SELECT id FROM t WHERE lower(b)='alpha';
DELETE FROM t WHERE id = 3;
SELECT id FROM t WHERE abs(a)=9 ORDER BY id;
SELECT id, a, b FROM t ORDER BY id;
PRAGMA integrity_check;
-- VACUUM rebuilds the file; the expression index must survive it.
VACUUM;
SELECT id FROM t WHERE abs(a)=9 ORDER BY id;
PRAGMA integrity_check;
