-- partialindex.html: "Create a partial index by adding a WHERE clause to the
-- end of an ordinary CREATE INDEX statement" and "The expression following
-- the WHERE clause may contain operators, literal values, and names of
-- columns in the table being indexed."  The index key may be an expression at
-- the same time (expridx.html), and neither changes an answer.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER);
INSERT INTO t VALUES(1,-5,1),(2,3,0),(3,-1,1),(4,7,0);
CREATE INDEX tp ON t(abs(a)) WHERE b > 0;
SELECT count(id) FROM t;
SELECT id FROM t WHERE abs(a)=5 AND b>0;
SELECT id FROM t WHERE abs(a)=7;
SELECT id, abs(a) FROM t ORDER BY id;
-- pragma.html, index_list: the fifth column is "'1' if the index is a partial
-- index and '0' if not" (PROFILE.md 3.5)
PRAGMA index_list(t);
-- "Only rows of the table for which the WHERE clause evaluates to true are
-- included in the index", so a UNIQUE partial index constrains only those
-- rows: a duplicate magnitude among the b>0 rows is rejected, while the same
-- magnitude among the b=0 rows is not.
CREATE UNIQUE INDEX tu ON t(abs(a)) WHERE b > 0;
INSERT INTO t VALUES(5,5,1);
SELECT count(id) FROM t;
INSERT INTO t VALUES(6,5,0);
SELECT count(id) FROM t;
SELECT id, a, b FROM t ORDER BY id;
PRAGMA integrity_check;
