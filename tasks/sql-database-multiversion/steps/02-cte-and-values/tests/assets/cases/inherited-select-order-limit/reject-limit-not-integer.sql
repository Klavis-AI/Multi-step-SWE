-- lang_select.html: "If the LIMIT expression evaluates to a NULL value or any
-- other value that cannot be losslessly converted to an integer, an error is
-- returned." The same lossless-integer rule applies to OFFSET.
CREATE TABLE t(a INTEGER);
INSERT INTO t VALUES(1),(2),(3);
SELECT a FROM t ORDER BY a LIMIT 1.5;
SELECT a FROM t ORDER BY a LIMIT NULL;
SELECT a FROM t ORDER BY a LIMIT 10 OFFSET NULL;
SELECT a FROM t ORDER BY a LIMIT '2.5';
SELECT count(*) FROM t;
