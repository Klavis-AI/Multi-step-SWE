-- R14.7: DROP VIEW removes exactly one schema row, leaving the base table's rows and a second view over it untouched.
-- DROP VIEW removes the schema row and nothing else: the table the view read
-- from keeps its rows and its own schema row, and a second view over the same
-- table is untouched.
CREATE TABLE base(id INTEGER PRIMARY KEY, tag TEXT);
INSERT INTO base VALUES(1,'x'),(2,'y');
CREATE VIEW v1 AS SELECT id FROM base;
CREATE VIEW v2 AS SELECT tag FROM base;
SELECT type, name FROM sqlite_schema ORDER BY name;
DROP VIEW v1;
SELECT type, name FROM sqlite_schema ORDER BY name;
SELECT tag FROM v2 ORDER BY tag;
SELECT count(*) FROM base;
