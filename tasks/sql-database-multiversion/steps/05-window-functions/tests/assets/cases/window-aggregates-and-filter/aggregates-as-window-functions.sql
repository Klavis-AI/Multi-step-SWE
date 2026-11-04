-- windowfunctions.html, Aggregate Window Functions: "for each row the result of
-- the aggregate window function is as if the corresponding aggregate were run
-- over all rows in the 'window frame'", and "All of SQLite's aggregate
-- functions may be used as aggregate window functions" -- read against step 1
-- section 4's closed list.  (PROFILE.md 5.5)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
SELECT k, count(*) OVER (ORDER BY k), sum(v) OVER (ORDER BY k),
          avg(v) OVER (ORDER BY k)
  FROM t ORDER BY k;
SELECT k, min(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          max(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING),
          total(v) OVER (ORDER BY k ROWS BETWEEN 1 PRECEDING AND 1 FOLLOWING)
  FROM t ORDER BY k;
-- count(*) and count(X) keep the meanings lang_aggfunc.html gives them
CREATE TABLE n(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO n VALUES(1,5),(2,NULL),(3,7),(4,NULL);
SELECT k, count(*) OVER (ORDER BY k), count(v) OVER (ORDER BY k),
          sum(v) OVER (ORDER BY k)
  FROM n ORDER BY k;
-- the storage class of each result is the one the plain aggregate returns
SELECT DISTINCT typeof(sum(v) OVER ()), typeof(avg(v) OVER ()),
                typeof(total(v) OVER ()), typeof(count(*) OVER ())
  FROM t;
