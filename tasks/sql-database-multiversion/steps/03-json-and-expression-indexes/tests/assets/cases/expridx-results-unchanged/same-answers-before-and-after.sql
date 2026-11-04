-- expridx.html: "Both of the above example queries would work fine without
-- the acctchng_magnitude index.  The acctchng_magnitude index index merely
-- helps the queries to run faster".  An index on an expression must therefore
-- change no answer -- which is what this case observes, and the only thing
-- about it the verifier ever observes: which index the planner picks, and
-- whether it picks one, is not scored (TEST_INTERFACE.md 6).
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b TEXT);
INSERT INTO t VALUES(1,-5,'Beta'),(2,3,'alpha'),(3,-1,'Gamma'),(4,7,'delta');
SELECT id FROM t WHERE abs(a) >= 3 ORDER BY id;
SELECT id, abs(a) FROM t ORDER BY abs(a), id;
SELECT lower(b) FROM t ORDER BY lower(b);
CREATE INDEX ta ON t(abs(a));
CREATE INDEX tb ON t(lower(b));
SELECT id FROM t WHERE abs(a) >= 3 ORDER BY id;
SELECT id, abs(a) FROM t ORDER BY abs(a), id;
SELECT lower(b) FROM t ORDER BY lower(b);
-- expridx.html: "the SQLite query planner insists that they be the same, not
-- merely equivalent", so a WHERE term the index cannot match must still give
-- the same answer -- it is only the plan that differs.
SELECT id FROM t WHERE a + 0 >= 3 ORDER BY id;
SELECT id FROM t WHERE upper(b) = 'BETA';
DROP INDEX ta;
DROP INDEX tb;
SELECT id FROM t WHERE abs(a) >= 3 ORDER BY id;
SELECT id, abs(a) FROM t ORDER BY abs(a), id;
SELECT lower(b) FROM t ORDER BY lower(b);
