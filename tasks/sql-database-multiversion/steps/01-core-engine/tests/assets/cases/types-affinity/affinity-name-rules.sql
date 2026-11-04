CREATE TABLE t(
  a INT, b INTEGER, c TINYINT, d BIGINT, e UNSIGNED BIG INT, f INT2, g INT8,
  h CHARACTER(20), i VARCHAR(255), j NCHAR(55), k TEXT, l CLOB,
  m BLOB, n,
  o REAL, p DOUBLE, q DOUBLE PRECISION, r FLOAT,
  s NUMERIC, t2 DECIMAL(10,5), u BOOLEAN, v STRING, w CHARINT, x FLOATING POINT
);
INSERT INTO t VALUES('1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1','1');
SELECT typeof(a), typeof(b), typeof(c), typeof(d), typeof(e), typeof(f), typeof(g) FROM t;
SELECT typeof(h), typeof(i), typeof(j), typeof(k), typeof(l) FROM t;
SELECT typeof(m), typeof(n) FROM t;
SELECT typeof(o), typeof(p), typeof(q), typeof(r) FROM t;
SELECT typeof(s), typeof(t2), typeof(u), typeof(v) FROM t;
SELECT typeof(w), typeof(x) FROM t;
