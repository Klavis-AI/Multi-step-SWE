-- PROFILE.md 5.10: the same rule covers "the key of an ordinary or UNIQUE
-- CREATE INDEX, the key expression of an index on an expression (step 3
-- section 3.4), and the WHERE clause of a partial index".  An index whose text
-- does not mention the column keeps its text unchanged, and sql stays NULL for
-- the automatic sqlite_autoindex_* indexes (step 1 section 6).
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, n INTEGER, m INTEGER);
CREATE INDEX i1 ON t(alpha);
CREATE UNIQUE INDEX i2 ON t(alpha, n);
CREATE INDEX i3 ON t(length(alpha));
CREATE INDEX i4 ON t(n) WHERE alpha IS NOT NULL;
CREATE INDEX i5 ON t(m);
SELECT name, sql FROM sqlite_master WHERE type = 'index' ORDER BY name;
ALTER TABLE t RENAME COLUMN alpha TO beta;
SELECT name, sql FROM sqlite_master WHERE type = 'index' ORDER BY name;
-- an automatic index has no stored text, before or after
CREATE TABLE u(a TEXT UNIQUE, epsilon INTEGER);
SELECT name, sql FROM sqlite_master WHERE tbl_name = 'u' ORDER BY name;
ALTER TABLE u RENAME COLUMN epsilon TO zeta;
SELECT name, sql FROM sqlite_master WHERE tbl_name = 'u' ORDER BY name;
PRAGMA integrity_check;
