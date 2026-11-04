-- partialindex.html at this release: the WHERE clause of a partial index "may
-- not contain subqueries, references to other tables, non-deterministic
-- functions, or bound parameters" (PROFILE.md 4.6, 4.7).  The pages name
-- random() and sqlite_version() as the functions whose result might change.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
CREATE TABLE o(id INTEGER PRIMARY KEY, n INTEGER);
INSERT INTO t VALUES(1,-5,'apple'),(2,3,'pear');
-- the permitted spellings, so the case fails if everything is rejected
CREATE INDEX ok1 ON t(a) WHERE abs(a) > 1;
CREATE INDEX ok2 ON t(abs(a)) WHERE a < 0;
-- a non-deterministic function is rejected
CREATE INDEX bad1 ON t(a) WHERE random() > 1;
CREATE INDEX bad2 ON t(a) WHERE sqlite_version() > '1';
CREATE INDEX bad3 ON t(abs(a)) WHERE a + random() > 1;
-- a subquery is rejected
CREATE INDEX bad4 ON t(a) WHERE id IN (SELECT id FROM o);
-- another table's column is rejected
CREATE INDEX bad5 ON t(a) WHERE o.n > 0;
-- a bound parameter is rejected, in either of the spellings step 1 section 3
-- puts in scope
CREATE INDEX bad6 ON t(a) WHERE a > ?;
CREATE INDEX bad7 ON t(a) WHERE a > :lim;
-- only the two permitted indexes exist, and the table still answers
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
SELECT id FROM t WHERE abs(a)=5 AND a<0 ORDER BY id;
SELECT count(*) FROM t;
PRAGMA integrity_check;
