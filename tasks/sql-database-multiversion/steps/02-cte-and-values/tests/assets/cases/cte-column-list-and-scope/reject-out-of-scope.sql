CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(4);
WITH c(x) AS (SELECT a FROM t) SELECT x FROM c;
-- lang_with.html: CTEs "exist only for the duration of a single SQL
-- statement", so the name is unknown to the next statement.
SELECT x FROM c;
SELECT count(*) FROM t;
