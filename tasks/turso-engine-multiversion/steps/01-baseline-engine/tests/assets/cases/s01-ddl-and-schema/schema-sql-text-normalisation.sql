-- R01.1: the normalised rendering of `sqlite_schema.sql`, over the spellings
-- `spec/PROFILE.md` rule 2 actually names.
--
-- The companion case `schema-sql-text.sql` grades one table. This one grades
-- the rule: three statements chosen so that every clause of rule 2 shows up in
-- the answer -- the space before a parenthesised column list, the space after
-- each comma, a type with parameters (`NUMERIC(10,2)`), a DEFAULT clause, a
-- table constraint re-printed after the column list (`PRIMARY KEY(x)`), and a
-- binary operator inside a CHECK (`salary >= 0`). A build that stores the
-- source bytes verbatim, or that normalises to some other spacing, disagrees
-- on the `sql` column while still agreeing on `type`, `name` and `tbl_name`.
CREATE TABLE people(id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER);
CREATE TABLE dept(code TEXT, label TEXT NOT NULL, budget NUMERIC(10,2) DEFAULT 0, note TEXT);
CREATE TABLE emp(id INTEGER, dept TEXT, salary INTEGER, PRIMARY KEY(id), CHECK (salary >= 0));
SELECT type, name, tbl_name FROM sqlite_schema ORDER BY name;
SELECT sql FROM sqlite_schema ORDER BY name;
