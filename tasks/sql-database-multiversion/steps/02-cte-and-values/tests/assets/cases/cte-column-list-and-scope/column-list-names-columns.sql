-- PROFILE.md section 2.1: a CTE's column-name list names its columns; without
-- a list its columns are named as its own query names them.
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'p'),(2,'q');
WITH s(k, v) AS (SELECT a, b FROM t) SELECT v, k FROM s ORDER BY k;
WITH s(k, v) AS (SELECT a, b FROM t) SELECT s.k FROM s WHERE s.v = 'q';
WITH s AS (SELECT a, b FROM t) SELECT a, b FROM s ORDER BY a;
WITH s AS (SELECT a AS kk, b AS vv FROM t) SELECT kk, vv FROM s ORDER BY kk;
WITH s(k, v) AS (VALUES(9,'z')) SELECT k, v FROM s;
