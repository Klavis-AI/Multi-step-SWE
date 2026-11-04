-- expridx.html, "Restrictions": "Expressions in CREATE INDEX statements may
-- only refer to columns of the table being indexed, not to columns in other
-- tables" and "Expressions in CREATE INDEX statements may not use
-- subqueries."  lang_createindex.html: "Expressions in an index may not
-- reference other tables and may not use subqueries".
CREATE TABLE t(a INTEGER, b INTEGER);
CREATE TABLE o(id INTEGER PRIMARY KEY, n INTEGER);
INSERT INTO t VALUES(-3,1),(2,2);
INSERT INTO o VALUES(1,10),(2,20);
-- an expression over the indexed table's own columns is accepted
CREATE INDEX ok1 ON t(a + b);
SELECT a FROM t WHERE a + b = -2;
-- a subquery is rejected
CREATE INDEX bad1 ON t((SELECT max(n) FROM o));
CREATE INDEX bad2 ON t(a + (SELECT 1));
CREATE INDEX bad3 ON t(a IN (SELECT id FROM o));
-- a reference to another table's column is rejected
CREATE INDEX bad4 ON t(o.n);
CREATE INDEX bad5 ON t(a + o.n);
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
SELECT count(*) FROM t;
SELECT count(*) FROM o;
