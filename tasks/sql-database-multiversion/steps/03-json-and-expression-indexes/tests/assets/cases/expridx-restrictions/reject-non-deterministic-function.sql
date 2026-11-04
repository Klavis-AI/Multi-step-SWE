-- expridx.html, "Restrictions": "Expressions in CREATE INDEX statmeent may
-- contain function calls, but only to functions whose output is always
-- determined completely by its input parameters (a.k.a.: 'deterministic'
-- functions).  Obviously, functions like random() will not work well in an
-- index.  But also functions like sqlite_version(), though they are constant
-- across any one database connection, are not constant across the life of the
-- underlying database file, and hence may not be used in a CREATE INDEX
-- statement."  lang_createindex.html says the same: "may not use ... functions
-- whose result might change (ex: random() or sqlite_version())".
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(-3,'p'),(2,'q');
-- a deterministic function is accepted
CREATE INDEX ok1 ON t(abs(a));
CREATE INDEX ok2 ON t(lower(b));
SELECT a FROM t WHERE abs(a)=3;
-- the two functions the pages name are rejected
CREATE INDEX bad1 ON t(random());
CREATE INDEX bad2 ON t(sqlite_version());
CREATE INDEX bad3 ON t(a + random());
CREATE INDEX bad4 ON t(a, sqlite_version());
-- nothing was created and the table still answers
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
SELECT count(*) FROM t;
SELECT b FROM t WHERE lower(b)='q';
