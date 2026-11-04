CREATE TABLE x07_24a(a int PRIMARY KEY, b text);
CREATE PUBLICATION x07_24p FOR TABLE x07_24a WHERE (a > 5) WITH (publish = 'insert');
SELECT p.pubname, p.pubinsert, p.pubupdate, t.rowfilter
  FROM pg_publication p JOIN pg_publication_tables t ON t.pubname = p.pubname
  WHERE p.pubname='x07_24p';
INSERT INTO x07_24a VALUES (1,'x');
UPDATE x07_24a SET b = 'y' WHERE a = 1;
SELECT a, b FROM x07_24a ORDER BY a;
DROP PUBLICATION x07_24p;
DROP TABLE x07_24a;
