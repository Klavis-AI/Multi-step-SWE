-- partialindex.html at this release: "The expression following the WHERE
-- clause may contain operators, literal values, and names of columns in the
-- table being indexed", and the clause "may not contain subqueries, references
-- to other tables, non-deterministic functions, or bound parameters".  A
-- deterministic function call is therefore allowed there, which is the rule
-- PROFILE.md 4.6 says supersedes step 3 section 3.4 from this step onward.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
INSERT INTO t VALUES(1,-5,'apple'),(2,3,'pear'),(3,-1,'plum'),(4,7,'apricot');
-- deterministic function calls in the WHERE clause
CREATE INDEX g1 ON t(a) WHERE abs(a) > 1;
CREATE INDEX g2 ON t(a) WHERE lower(b) = 'pear';
CREATE INDEX g3 ON t(id) WHERE length(b) > 4;
-- LIKE and GLOB are the function spellings like() and glob() (step 1
-- section 4) and are allowed here too
CREATE INDEX g4 ON t(a) WHERE b LIKE 'ap%';
CREATE INDEX g5 ON t(a) WHERE b GLOB 'p*';
-- a function in the index KEY at the same time, which step 3 section 3.4
-- already allowed
CREATE INDEX g6 ON t(abs(a)) WHERE lower(b) <> 'plum';
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
-- the indexes change no answer (step 3 section 3.4's first consequence)
SELECT id FROM t WHERE abs(a) > 1 ORDER BY id;
SELECT id FROM t WHERE b LIKE 'ap%' ORDER BY id;
SELECT id FROM t WHERE b GLOB 'p*' ORDER BY id;
SELECT id FROM t WHERE lower(b) <> 'plum' AND abs(a) = 5 ORDER BY id;
PRAGMA integrity_check;
-- a UNIQUE partial index whose WHERE clause calls a function constrains only
-- the rows that clause selects (partialindex.html, Unique Partial Indexes)
CREATE TABLE s(id INTEGER PRIMARY KEY, grp TEXT, tag TEXT);
CREATE UNIQUE INDEX su ON s(grp) WHERE length(tag) > 2;
INSERT INTO s VALUES(1,'g','long'),(2,'g','ab'),(3,'g','xy');
SELECT id,grp,tag FROM s ORDER BY id;
-- pragma.html, index_list on a table carrying exactly one index
-- (PROFILE.md 3.5)
PRAGMA index_list(s);
-- a second long-tagged row in group g duplicates an indexed key
INSERT INTO s VALUES(4,'g','also-long') ON CONFLICT(grp) WHERE length(tag) > 2
  DO UPDATE SET tag=excluded.tag;
SELECT id,grp,tag FROM s ORDER BY id;
SELECT count(*) FROM s;
PRAGMA integrity_check;
