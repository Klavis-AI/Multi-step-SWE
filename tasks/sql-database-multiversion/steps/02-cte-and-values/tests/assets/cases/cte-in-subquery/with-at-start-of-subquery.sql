-- lang_with.html: "The WITH clause must appear at the beginning of a top-level
-- SELECT statement or at the beginning of a subquery."
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(5),(6);
SELECT * FROM (WITH q(z) AS (SELECT 7) SELECT z FROM q);
SELECT * FROM (WITH q(z) AS (SELECT a FROM t) SELECT z FROM q) ORDER BY 1;
SELECT * FROM (WITH q(z) AS (VALUES(1),(2)) SELECT sum(z) FROM q);
SELECT * FROM (WITH q(z) AS (SELECT a FROM t) SELECT z FROM q WHERE z = 6)
  JOIN t ON 1 = 1 ORDER BY t.a;
