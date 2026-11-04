-- PROFILE.md 5.1: "one arm of a compound select" is one of the SELECTs a
-- window function may appear in.  A compound select's arms are step 1 section
-- 1's, and a window function is computed over the arm's own result set.
CREATE TABLE a(k INTEGER PRIMARY KEY, v INTEGER);
CREATE TABLE b(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO a VALUES(1,10),(2,20);
INSERT INTO b VALUES(3,30),(4,40),(5,50);
-- one arm carries a window function and numbers its own rows
SELECT k, row_number() OVER (ORDER BY k) AS rn FROM a
UNION ALL
SELECT k, 0 FROM b
ORDER BY k;
-- a window function over the whole compound, computed in an enclosing SELECT
SELECT k, row_number() OVER (ORDER BY k) AS rn
  FROM (SELECT k FROM a UNION ALL SELECT k FROM b)
  ORDER BY k;
-- UNION removes duplicates before the enclosing window function sees them
SELECT v, count(*) OVER () AS n
  FROM (SELECT 10 AS v UNION SELECT 10 UNION SELECT 20)
  ORDER BY v;
