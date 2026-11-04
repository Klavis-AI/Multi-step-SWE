-- lang_createtable.html: "The expression of a CHECK constraint may not
-- contain a subquery."  PROFILE.md section 1 puts CHECK in scope with that
-- page.
CREATE TABLE u(x);
INSERT INTO u VALUES(1);
-- a CHECK over the table's own columns is the permitted form
CREATE TABLE ok(a, CHECK(a > 0));
INSERT INTO ok VALUES(2);
-- a subquery in a column CHECK, in a table CHECK, and inside EXISTS
CREATE TABLE bad1(a, CHECK(a IN (SELECT x FROM u)));
CREATE TABLE bad2(a, CHECK(a = (SELECT max(x) FROM u)));
CREATE TABLE bad3(a, b, CHECK(EXISTS(SELECT 1 FROM u WHERE x = a)));
SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;
SELECT a FROM ok;
