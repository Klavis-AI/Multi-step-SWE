CREATE TABLE x05_08a(i int, v int);
INSERT INTO x05_08a SELECT g, g/3 FROM generate_series(1,9) g;
CREATE VIEW x05_08v AS SELECT * FROM x05_08a ORDER BY v FETCH FIRST 2 ROWS WITH TIES;
SELECT * FROM x05_08v ORDER BY i;
SELECT pg_get_viewdef('x05_08v'::regclass, true);
SELECT table_name FROM information_schema.views WHERE table_name='x05_08v';
DROP VIEW x05_08v;
DROP TABLE x05_08a;
