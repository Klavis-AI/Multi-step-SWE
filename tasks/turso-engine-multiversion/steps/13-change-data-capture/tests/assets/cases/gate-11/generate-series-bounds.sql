-- R11.5: generate_series in a FROM clause -- an ascending range, an explicit
-- stride, negative bounds, aggregates over the series, and a range whose
-- start is past its stop, which yields no rows at all.
-- generate_series is a table-valued function: it belongs in a FROM clause,
-- and its three-argument form steps by the given stride. Stock sqlite3 gives
-- the same rows for the same bounds, which is what makes the bounds and not
-- only the function name the graded fact.
SELECT value FROM generate_series(1,4);
SELECT value FROM generate_series(0,10,5);
SELECT value FROM generate_series(-2,2);
SELECT count(*) FROM generate_series(1,100);
SELECT sum(value) FROM generate_series(1,10);
SELECT count(*) FROM generate_series(5,1);
