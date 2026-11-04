-- PROFILE.md 5.10: after ALTER TABLE t RENAME COLUMN old TO new, the sql
-- column of every sqlite_master row for t holds the text it held before, with
-- each occurrence of the column identifier old replaced by new, and nothing
-- else changed -- from lang_altertable.html ("works by modifing the SQL text of
-- the schema stored in the sqlite_master table"; "The column name is changed
-- ... within the table definition itself") and fileformat2.html section 2.6.
-- Every CREATE statement here is written already-normalised, and both names are
-- ordinary unquoted identifiers that appear nowhere else in the text.
CREATE TABLE t(k INTEGER PRIMARY KEY, alpha TEXT, n INTEGER);
SELECT sql FROM sqlite_master WHERE name = 't';
ALTER TABLE t RENAME COLUMN alpha TO beta;
SELECT sql FROM sqlite_master WHERE name = 't';
-- the renamed column keeps its constraints and its position in the text
CREATE TABLE u(a INTEGER, gamma TEXT NOT NULL DEFAULT 'z', b REAL, CHECK(gamma <> ''));
SELECT sql FROM sqlite_master WHERE name = 'u';
ALTER TABLE u RENAME COLUMN gamma TO delta;
SELECT sql FROM sqlite_master WHERE name = 'u';
-- a column the rename does not touch keeps its own name in the text, and the
-- old name here is deliberately a one-letter identifier that is a SUBSTRING of
-- delta, REAL, DEFAULT and TABLE: PROFILE.md 5.10 replaces each occurrence of
-- the column IDENTIFIER and nothing else, so a plain substring rewrite of the
-- stored text would produce deltaa here and is not enough.
ALTER TABLE u RENAME COLUMN a TO aa;
SELECT sql FROM sqlite_master WHERE name = 'u';
PRAGMA integrity_check;
