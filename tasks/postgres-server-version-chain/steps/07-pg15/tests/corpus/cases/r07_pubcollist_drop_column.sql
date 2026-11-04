CREATE TABLE x07_30a(a int PRIMARY KEY, b text, c int);
CREATE PUBLICATION x07_30p FOR TABLE x07_30a (a, b);
SELECT attnames FROM pg_publication_tables WHERE pubname='x07_30p';
ALTER TABLE x07_30a DROP COLUMN c;
SELECT attnames FROM pg_publication_tables WHERE pubname='x07_30p';
ALTER TABLE x07_30a DROP COLUMN b;
SELECT count(*) FROM pg_publication WHERE pubname='x07_30p';
DROP PUBLICATION IF EXISTS x07_30p;
DROP TABLE x07_30a;
