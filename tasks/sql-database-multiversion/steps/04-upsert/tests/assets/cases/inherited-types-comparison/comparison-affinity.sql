CREATE TABLE t1(
  a TEXT,
  b NUMERIC,
  c BLOB,
  d
);
INSERT INTO t1 VALUES('500', '500', '500', 500);
SELECT typeof(a), typeof(b), typeof(c), typeof(d) FROM t1;
SELECT a < 40, a < 60, a < 600 FROM t1;
SELECT a < '40', a < '60', a < '600' FROM t1;
SELECT b < 40, b < 60, b < 600 FROM t1;
SELECT b < '40', b < '60', b < '600' FROM t1;
SELECT c < 40, c < 60, c < 600 FROM t1;
SELECT c < '40', c < '60', c < '600' FROM t1;
SELECT d < 40, d < 60, d < 600 FROM t1;
SELECT d < '40', d < '60', d < '600' FROM t1;
