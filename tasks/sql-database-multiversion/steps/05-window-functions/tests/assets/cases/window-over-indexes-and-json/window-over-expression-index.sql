-- PROFILE.md 5.12: step 3's indexes on expressions keep exactly the meaning
-- step 3 section 3.4 gave them, and a windowed query is ordinary work over
-- them.  The window ORDER BY and the index key are the same expression here,
-- which is the case an implementation is most likely to get wrong.
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
CREATE INDEX ti ON t(abs(v));
INSERT INTO t VALUES(1,-30),(2,10),(3,-20),(4,20);
SELECT k, abs(v) AS a,
       rank() OVER (ORDER BY abs(v)) AS rk,
       dense_rank() OVER (ORDER BY abs(v)) AS dr,
       sum(abs(v)) OVER (ORDER BY abs(v)) AS running
  FROM t ORDER BY k;
-- a partial index and a window function over the rows it covers
CREATE TABLE p(k INTEGER PRIMARY KEY, v INTEGER, active INTEGER);
CREATE INDEX pi ON p(v) WHERE active;
INSERT INTO p VALUES(1,10,1),(2,20,0),(3,30,1),(4,40,1);
SELECT k, v, sum(v) OVER (ORDER BY k) AS running
  FROM p WHERE active ORDER BY k;
-- the indexes are unaffected by having been read through a window function
PRAGMA integrity_check;
