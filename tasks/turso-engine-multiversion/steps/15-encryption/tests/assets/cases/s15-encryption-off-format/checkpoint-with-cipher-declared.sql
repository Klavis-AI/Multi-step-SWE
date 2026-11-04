-- R15.1 and R15.14: the pager is rewritten at this checkpoint to carry a
-- page transform, so the checkpoint path has to keep working underneath it.
-- With a cipher declared and no key -- configured, not engaged -- every
-- checkpoint mode still returns SQLite's result triple and every row stays
-- readable through the pager afterwards.
-- The pager is rewritten at this checkpoint to carry a page transform, so the
-- checkpoint path has to keep working underneath it. With a cipher declared
-- but no key -- the transform configured and not engaged -- every checkpoint
-- mode still returns SQLite's result triple and every row stays readable
-- through the pager afterwards.
PRAGMA cipher = 'aegis256';
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'a'),(2,'b');
PRAGMA wal_checkpoint(TRUNCATE);
SELECT id, v FROM t ORDER BY id;
INSERT INTO t VALUES(3,'c');
PRAGMA wal_checkpoint(FULL);
SELECT id, v FROM t ORDER BY id;
PRAGMA cipher;
