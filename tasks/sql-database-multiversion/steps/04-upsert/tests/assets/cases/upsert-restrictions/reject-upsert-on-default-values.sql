-- lang_insert.html: "The upsert-clause is not supported after DEFAULT VALUES",
-- and again "The upsert clause is not allowed on an 'INSERT ... DEFAULT
-- VALUES'".  (PROFILE.md 4.7)
CREATE TABLE t(k INTEGER PRIMARY KEY, c TEXT DEFAULT 'd');
-- INSERT ... DEFAULT VALUES on its own is step 1 section 1 syntax and works
INSERT INTO t DEFAULT VALUES;
SELECT k,c FROM t ORDER BY k;
-- with a DO NOTHING clause it is rejected
INSERT INTO t DEFAULT VALUES ON CONFLICT DO NOTHING;
SELECT count(*) FROM t;
-- with a DO UPDATE clause it is rejected too
INSERT INTO t DEFAULT VALUES ON CONFLICT(k) DO UPDATE SET c='x';
SELECT count(*) FROM t;
-- the same two upsert clauses on a VALUES form of the same INSERT are fine
INSERT INTO t(k,c) VALUES(1,'D1') ON CONFLICT(k) DO UPDATE SET c=excluded.c;
INSERT INTO t(k,c) VALUES(2,'two') ON CONFLICT(k) DO NOTHING;
SELECT k,c FROM t ORDER BY k;
PRAGMA integrity_check;
