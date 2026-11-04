CREATE TABLE x07_29a(a int PRIMARY KEY, b text, c int);
CREATE PUBLICATION x07_29p FOR TABLE x07_29a (a, b) WHERE (a > 1);
SELECT pubname, attnames, rowfilter FROM pg_publication_tables WHERE pubname='x07_29p';
ALTER PUBLICATION x07_29p SET TABLE x07_29a (a, c);
SELECT pubname, attnames, rowfilter FROM pg_publication_tables WHERE pubname='x07_29p';
DROP PUBLICATION x07_29p;
DROP TABLE x07_29a;
