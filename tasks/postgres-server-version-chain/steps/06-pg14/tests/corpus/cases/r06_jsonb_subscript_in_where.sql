CREATE TABLE x06_29a(id int, j jsonb);
INSERT INTO x06_29a VALUES (1,'{"k":"a","n":1}'),(2,'{"k":"b","n":2}'),(3,'{"k":"a","n":3}');
SELECT id FROM x06_29a WHERE j['k'] = '"a"' ORDER BY id;
SELECT id, j['n'] AS n FROM x06_29a WHERE (j['n'])::text::int > 1 ORDER BY id;
SELECT j['k'] AS k, count(*) FROM x06_29a GROUP BY j['k'] ORDER BY k;
CREATE INDEX x06_29i ON x06_29a ((j['k']));
SELECT count(*) FROM x06_29a WHERE j['k'] = '"b"';
DROP TABLE x06_29a;
