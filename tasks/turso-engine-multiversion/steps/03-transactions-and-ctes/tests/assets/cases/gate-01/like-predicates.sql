-- R01.2: LIKE as a WHERE predicate, wildcard by wildcard, against stock
-- sqlite3.
--
-- R01.2 asks for row-for-row agreement with stock sqlite3 on a script mixing
-- INSERT and SELECT with WHERE and ORDER BY. LIKE is the predicate this case
-- puts in the WHERE, and the matrix replays the whole script against stock
-- sqlite3 automatically, so the agreement is measured rather than asserted.
-- This case is also milestone 1's gate, so its expected output is re-measured
-- at every later checkpoint in the chain and must not change.
--
-- LIKE with each wildcard in each position. LIKE is case-insensitive for ASCII
-- letters, '%' matches any run including an empty one, and '_' matches exactly
-- one character -- so the pattern 'a' matches only the single-character row
-- while '_' matches it too and nothing longer.
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
INSERT INTO t VALUES(1,'apple');
INSERT INTO t VALUES(2,'apricot');
INSERT INTO t VALUES(3,'banana');
INSERT INTO t VALUES(4,'APPLE');
INSERT INTO t VALUES(5,'a');
SELECT 'prefix', id, s FROM t WHERE s LIKE 'ap%' ORDER BY id;
SELECT 'suffix', id, s FROM t WHERE s LIKE '%e' ORDER BY id;
SELECT 'middle', id, s FROM t WHERE s LIKE '%an%' ORDER BY id;
SELECT 'one char', id, s FROM t WHERE s LIKE '_' ORDER BY id;
SELECT 'two wildcards', id, s FROM t WHERE s LIKE 'a_p%' ORDER BY id;
SELECT 'exact', count(*) FROM t WHERE s LIKE 'apple';
SELECT 'upper pattern', count(*) FROM t WHERE s LIKE 'APPLE';
SELECT 'every row', count(*) FROM t WHERE s LIKE '%';
SELECT 'five characters', count(*) FROM t WHERE s LIKE '_____';
SELECT count(*) FROM t WHERE s LIKE 'z%';
