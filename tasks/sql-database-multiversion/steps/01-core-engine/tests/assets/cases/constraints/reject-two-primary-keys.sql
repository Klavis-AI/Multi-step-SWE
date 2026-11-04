-- lang_createtable.html, "SQL Data Constraints": "Each table in SQLite may
-- have at most one PRIMARY KEY. ... If there is more than one PRIMARY KEY
-- clause in a single CREATE TABLE statement, it is an error."  PROFILE.md
-- section 1 puts PRIMARY KEY in both the column and the table form in scope
-- with that page.
-- the permitted spellings first: one column-form and one table-form key
CREATE TABLE ok1(a PRIMARY KEY, b);
CREATE TABLE ok2(a, b, PRIMARY KEY(a, b));
INSERT INTO ok1 VALUES(1, 'p');
-- two column-form PRIMARY KEY clauses
CREATE TABLE bad1(a PRIMARY KEY, b PRIMARY KEY);
-- two table-form PRIMARY KEY clauses
CREATE TABLE bad2(a, b, PRIMARY KEY(a), PRIMARY KEY(b));
-- one of each
CREATE TABLE bad3(a PRIMARY KEY, b, PRIMARY KEY(b));
SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;
SELECT a, b FROM ok1;
