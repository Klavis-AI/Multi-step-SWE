-- R03.17: CAST between the storage classes -- leading numeric prefixes,
-- truncation toward zero, NUMERIC affinity and NULL -- answered exactly as
-- stock sqlite3 answers it.
-- CAST between the storage classes, answered the way stock sqlite3 answers it.
-- A cast to INTEGER takes the leading numeric prefix and yields 0 when there is
-- not one, a cast of a negative real truncates toward zero rather than toward
-- minus infinity, and a cast of NULL stays NULL whatever it is cast to.
SELECT cast('12abc' AS INTEGER), cast('abc' AS INTEGER), cast('' AS INTEGER);
SELECT cast(3.9 AS INTEGER), cast(-3.9 AS INTEGER), cast(0.5 AS INTEGER), cast(-0.5 AS INTEGER);
SELECT cast('2.5' AS REAL), cast(5 AS REAL), cast('x' AS REAL);
SELECT cast(7 AS TEXT), typeof(cast(7 AS TEXT)), cast(2.5 AS TEXT);
SELECT typeof(cast('ab' AS BLOB)), length(cast('ab' AS BLOB));
SELECT typeof(cast(1 AS NUMERIC)), cast('3.5' AS NUMERIC), typeof(cast('3.5' AS NUMERIC));
SELECT cast(NULL AS INTEGER) IS NULL, typeof(cast(NULL AS TEXT)), typeof(cast(NULL AS REAL));
SELECT cast(cast('42' AS INTEGER) AS TEXT), typeof(cast(cast(42 AS TEXT) AS INTEGER));
