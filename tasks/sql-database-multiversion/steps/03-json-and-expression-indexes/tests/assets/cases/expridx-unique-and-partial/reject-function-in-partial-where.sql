-- partialindex.html: "The WHERE clause may not contains subqueries,
-- references to other tables, functions, or bound parameters.  The LIKE,
-- GLOB, MATCH, and REGEXP operators in SQLite are implemented as functions by
-- the same name.  Since functions are prohibited in the WHERE clause of a
-- CREATE INDEX statement, so too are the LIKE, GLOB, MATCH, and REGEXP
-- operators."  This restriction is on the WHERE clause only: expridx.html
-- allows a function call in the index key of the same statement.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
CREATE TABLE o(id INTEGER PRIMARY KEY, n INTEGER);
INSERT INTO t VALUES(1,-5,'apple'),(2,3,'pear');
-- a function in the KEY with an operator-only WHERE clause is accepted
CREATE INDEX ok1 ON t(abs(a)) WHERE a < 0;
CREATE INDEX ok2 ON t(lower(b)) WHERE id > 0;
SELECT id FROM t WHERE abs(a)=5 AND a<0;
-- a function in the WHERE clause is rejected
CREATE INDEX bad1 ON t(a) WHERE abs(a) > 1;
CREATE INDEX bad2 ON t(abs(a)) WHERE lower(b) = 'pear';
-- LIKE and GLOB are functions by the same rule
CREATE INDEX bad3 ON t(a) WHERE b LIKE 'a%';
CREATE INDEX bad4 ON t(a) WHERE b GLOB 'a*';
-- a subquery and another table's column are rejected there too
CREATE INDEX bad5 ON t(a) WHERE id IN (SELECT id FROM o);
CREATE INDEX bad6 ON t(a) WHERE o.n > 0;
-- and so is a bound parameter, in either of the spellings step 1 section 3
-- puts in scope
CREATE INDEX bad7 ON t(a) WHERE a > ?;
CREATE INDEX bad8 ON t(a) WHERE a > :lim;
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
SELECT id FROM t WHERE lower(b)='pear';
