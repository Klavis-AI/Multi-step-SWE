-- R05.5: a deferred transaction that never writes opens and closes cleanly,
-- and leaves the rows a later query sees exactly as it found them.
--
-- The aggregates inside the transaction and the identical aggregates after
-- COMMIT are the observation: the same three rows and the same sum, so
-- opening and closing an empty deferred transaction changed nothing.
CREATE TABLE t(id INTEGER PRIMARY KEY, n INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30);
BEGIN DEFERRED;
SELECT count(*), sum(n) FROM t;
SELECT n FROM t WHERE id=2;
COMMIT;
SELECT count(*), sum(n) FROM t;
