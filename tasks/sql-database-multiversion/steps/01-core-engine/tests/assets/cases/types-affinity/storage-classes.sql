CREATE TABLE t1(
  t TEXT,
  nu NUMERIC,
  i INTEGER,
  r REAL,
  no BLOB
);
INSERT INTO t1 VALUES('500.0', '500.0', '500.0', '500.0', '500.0');
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;
DELETE FROM t1;
INSERT INTO t1 VALUES(500.0, 500.0, 500.0, 500.0, 500.0);
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;
DELETE FROM t1;
INSERT INTO t1 VALUES(500, 500, 500, 500, 500);
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;
DELETE FROM t1;
INSERT INTO t1 VALUES(x'0500', x'0500', x'0500', x'0500', x'0500');
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;
DELETE FROM t1;
INSERT INTO t1 VALUES(NULL,NULL,NULL,NULL,NULL);
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;

-- PROFILE.md section 2: affinity is applied on assignment by UPDATE as well as
-- by INSERT. Same five columns, same five affinities, values assigned the
-- other way round.
UPDATE t1 SET t = 500.0, nu = '500.0', i = '500.0', r = 500, no = '500.0';
SELECT typeof(t), typeof(nu), typeof(i), typeof(r), typeof(no) FROM t1;
SELECT t, nu, i, r, no FROM t1;
