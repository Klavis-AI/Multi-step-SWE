-- partialindex.html, "Creating Partial Indexes": "The expression following the
-- WHERE clause may contain operators, literal values, and names of columns in
-- the table being indexed.  The WHERE clause may not contains subqueries,
-- references to other tables, functions, or bound parameters."  PROFILE.md
-- section 1 puts the partial-index WHERE clause in scope with that page.  The
-- "functions" half of that sentence is exercised by
-- reject-function-in-where.sql; the other three prohibitions are here.
CREATE TABLE t(a, b);
CREATE TABLE o(c);
INSERT INTO o VALUES(5);
-- the permitted spelling: operators, literal values and this table's columns
CREATE INDEX ok1 ON t(a) WHERE b > 3 AND b IS NOT NULL;
-- a subquery
CREATE INDEX bad1 ON t(a) WHERE b IN (SELECT c FROM o);
CREATE INDEX bad2 ON t(a) WHERE b = (SELECT max(c) FROM o);
-- another table's column
CREATE INDEX bad3 ON t(a) WHERE o.c > 1;
-- a bound parameter, in two of the spellings PROFILE.md section 3 has in scope
CREATE INDEX bad4 ON t(a) WHERE b > ?;
CREATE INDEX bad5 ON t(a) WHERE b > :lim;
SELECT name FROM sqlite_master WHERE type = 'index' ORDER BY name;
INSERT INTO t VALUES(1, 9);
SELECT a FROM t WHERE b > 3;
