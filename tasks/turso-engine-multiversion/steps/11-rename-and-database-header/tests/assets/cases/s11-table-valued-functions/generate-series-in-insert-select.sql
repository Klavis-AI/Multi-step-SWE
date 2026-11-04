-- R11.5: the series is usable as the source of an INSERT ... SELECT, and the
-- rows it inserted read back from the table afterwards.
-- The function's rows have to be insertable, not only selectable: an
-- INSERT ... SELECT from generate_series must leave a table whose contents
-- stock sqlite3 confirms.
-- The column the series projects is named `value` -- that is the eponymous
-- virtual table's schema, `CREATE TABLE generate_series(value, start HIDDEN,
-- stop HIDDEN, step HIDDEN)`, in spec/sqlite/vtab.html, and it is the name
-- every statement below selects.
CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER);
INSERT INTO t SELECT value, value * value FROM generate_series(1,5);
SELECT count(*), sum(a) FROM t;
SELECT id, a FROM t ORDER BY id;
-- The stride may be negative, and then the series descends rather than
-- yielding nothing: `generate_series(10,0,-2)` is six rows counting down, and
-- a range whose start equals its stop is one row rather than none. Both are
-- sources of rows like any other, so both are exercised through an
-- INSERT ... SELECT and read back. This case is a .sql case, so gen-matrix.sh
-- replays it against stock sqlite3 3.50.2 and rejects it unless stock returns
-- the same rows -- which is what makes the stride domain a measurement here
-- and not this engine's word.
CREATE TABLE d(a INTEGER);
INSERT INTO d SELECT value FROM generate_series(10,0,-2);
INSERT INTO d SELECT value FROM generate_series(3,3);
SELECT count(*), sum(a) FROM d;
SELECT a FROM d ORDER BY a;
