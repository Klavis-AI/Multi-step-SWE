CREATE TABLE t(a INTEGER, b);
INSERT INTO t VALUES(1,'x'),(2,'x'),(4,NULL),(5,'y'),(NULL,'z');
SELECT count(*), count(a), count(b), count(DISTINCT b) FROM t;
SELECT sum(a), typeof(sum(a)), total(a), typeof(total(a)) FROM t;
SELECT avg(a), typeof(avg(a)) FROM t;
SELECT sum(DISTINCT a) FROM t;
-- PROFILE.md section 4: each aggregate with and without DISTINCT, wherever
-- the form takes an argument to be distinct over.
SELECT avg(DISTINCT a), total(DISTINCT a), count(DISTINCT a) FROM t;
SELECT typeof(sum(a)) FROM (SELECT 1.5 AS a UNION ALL SELECT 2.5);
DELETE FROM t;
SELECT sum(a) IS NULL, total(a), count(*), avg(a) IS NULL FROM t;
