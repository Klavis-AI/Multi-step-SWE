-- R15.14: relaxing the synchronous mode changes when the pager fsyncs and
-- never what it stores -- rows written with synchronous off checkpoint into
-- the main database and read back unchanged, and the checkpoint returns the
-- same result triple it returns in full mode.
-- Relaxing the synchronous mode changes when the pager fsyncs, never what it
-- stores: rows written with synchronous off are checkpointed into the main
-- database and read back unchanged, and the checkpoint returns the same
-- result triple it returns in full mode.
PRAGMA synchronous = OFF;
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b'),(3,'c');
PRAGMA wal_checkpoint(TRUNCATE);
SELECT id, v FROM t ORDER BY id;
PRAGMA synchronous = FULL;
INSERT INTO t VALUES(4,'d');
PRAGMA wal_checkpoint(TRUNCATE);
SELECT count(*) FROM t;
PRAGMA synchronous;
