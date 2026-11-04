CREATE TABLE x07_20a(a int PRIMARY KEY, b text);
CREATE PUBLICATION x07_20p FOR TABLE x07_20a WHERE (a > 5);
SELECT rowfilter FROM pg_publication_tables WHERE pubname='x07_20p';
ALTER PUBLICATION x07_20p SET TABLE x07_20a WHERE (a < 5);
SELECT rowfilter FROM pg_publication_tables WHERE pubname='x07_20p';
ALTER PUBLICATION x07_20p SET TABLE x07_20a;
SELECT rowfilter IS NULL AS no_filter FROM pg_publication_tables WHERE pubname='x07_20p';
DROP PUBLICATION x07_20p;
DROP TABLE x07_20a;
