-- printf() over a value of each of the five storage classes, each with the
-- conversion PROFILE.md section 2.3 pairs with it.
CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n);
INSERT INTO t VALUES(42, 2.5, 'hi', X'4142', NULL);
SELECT printf('[%d]', i), printf('[%.2f]', r), printf('[%s]', s),
       printf('[%s]', hex(b)), printf('[%s]', CAST(b AS TEXT)),
       printf('[%s]', n), printf('[%d]', n) FROM t;
SELECT typeof(printf('%d', i)), typeof(printf('%s', s)) FROM t;
WITH v(k) AS (VALUES(7)) SELECT printf('cte=%d', k) FROM v;
SELECT printf('%s', printf('%d', 3));
