-- fileformat2.html, "Storage Of The SQL Database Schema": sqlite_master holds
-- one row per index, whose sql column is the text of the CREATE statement
-- that made it.  An index on an expression is an ordinary index there.
CREATE TABLE t(a INTEGER, b INTEGER);
INSERT INTO t VALUES(-3,1),(2,2);
CREATE INDEX ti ON t(abs(a));
SELECT type, name, tbl_name FROM sqlite_master WHERE name='ti';
SELECT sql FROM sqlite_master WHERE name='ti';
SELECT rootpage > 0 FROM sqlite_master WHERE name='ti';
-- pragma.html, index_list: "A sequence number ...", "The name of the index",
-- "'1' if the index is UNIQUE and '0' if not", "'c' if the index was created
-- by a CREATE INDEX statement ...", "'1' if the index is a partial index and
-- '0' if not".  This is PROFILE.md 3.5's five-column shape; each table below
-- carries exactly one index so no row order is compared.
PRAGMA index_list(t);
CREATE TABLE u(a INTEGER, b INTEGER);
CREATE UNIQUE INDEX ui ON u(a*2);
PRAGMA index_list(u);
CREATE TABLE v(a INTEGER, b INTEGER);
CREATE INDEX vi ON v(abs(a)) WHERE b > 0;
PRAGMA index_list(v);
-- pragma.html gives origin the three values "'c' if the index was created by
-- a CREATE INDEX statement, 'u' if the index was created by a UNIQUE
-- constraint, or 'pk' if the index was created by a PRIMARY KEY constraint",
-- and fileformat2.html "Internal Schema Objects" names an index SQLite makes
-- for such a constraint "sqlite_autoindex_TABLE_N" (PROFILE.md 6).  Each of
-- the two tables below carries exactly one index, so no row order is compared.
CREATE TABLE w(a INTEGER UNIQUE, b INTEGER);
PRAGMA index_list(w);
CREATE TABLE x(a INTEGER, b INTEGER, PRIMARY KEY(a,b));
PRAGMA index_list(x);
-- lang_createindex.html: "Indexes are removed with the DROP INDEX command."
DROP INDEX ti;
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
SELECT a FROM t WHERE abs(a)=3;
DROP INDEX IF EXISTS ti;
SELECT count(*) FROM sqlite_master WHERE type='index' AND tbl_name='t';
PRAGMA integrity_check;
