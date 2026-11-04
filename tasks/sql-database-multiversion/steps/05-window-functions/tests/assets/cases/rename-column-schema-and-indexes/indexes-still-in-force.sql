-- PROFILE.md 5.9: "every index on the table stays attached and stays in force:
-- a UNIQUE index still rejects a duplicate, a partial index still covers
-- exactly the rows its WHERE clause selects, an index on an expression (step 3
-- section 3.4) still answers the same queries, and PRAGMA integrity_check still
-- reports ok."
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, n INTEGER);
CREATE UNIQUE INDEX u ON t(alpha);
CREATE INDEX p ON t(n) WHERE alpha IS NOT NULL;
CREATE INDEX e ON t(length(alpha));
INSERT INTO t VALUES(1,'aa',10),(2,'bbb',20),(3,NULL,30);
ALTER TABLE t RENAME COLUMN alpha TO beta;
-- the expression index still answers its query
SELECT k FROM t WHERE length(beta) = 3;
SELECT k, length(beta) FROM t WHERE beta IS NOT NULL ORDER BY k;
-- the partial index still covers exactly the rows its WHERE clause selects
SELECT count(*) FROM t WHERE beta IS NOT NULL;
SELECT k, n FROM t WHERE beta IS NOT NULL AND n > 5 ORDER BY n;
-- the UNIQUE index still rejects a duplicate
INSERT INTO t VALUES(4,'aa',40);
SELECT k, beta, n FROM t ORDER BY k;
-- and still admits a value it has not seen
INSERT INTO t VALUES(5,'cccc',50);
SELECT k, beta, n FROM t ORDER BY k;
PRAGMA integrity_check;
