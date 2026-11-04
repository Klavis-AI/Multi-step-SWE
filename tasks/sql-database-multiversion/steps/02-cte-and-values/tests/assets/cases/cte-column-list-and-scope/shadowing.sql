-- PROFILE.md section 2.1: inside the statement that defines it a CTE name
-- takes precedence over a real table of the same name; everywhere else the
-- name means the real table again.
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(99);
WITH t(a) AS (SELECT 1) SELECT a FROM t;
WITH t(a) AS (VALUES(7),(8)) SELECT sum(a) FROM t;
SELECT a FROM t;
WITH other(x) AS (SELECT 5) SELECT a FROM t;
SELECT count(*) FROM t;
