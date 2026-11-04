CREATE TABLE r09sq1(id int, doc jsonb);
INSERT INTO r09sq1 VALUES (1,'{"n":1,"s":"a"}'),(2,'{"n":2}'),(3,'{"s":"c"}');
SELECT id FROM r09sq1 WHERE JSON_EXISTS(doc, '$.n') ORDER BY id;
SELECT id, JSON_VALUE(doc, '$.n' RETURNING int DEFAULT -1 ON EMPTY) AS n FROM r09sq1 ORDER BY id;
SELECT id, JSON_QUERY(doc, '$.s') AS s FROM r09sq1 ORDER BY id;
SELECT id, JSON_VALUE(doc, '$.s' DEFAULT 'zz' ON EMPTY) FROM r09sq1 ORDER BY id;
DROP TABLE r09sq1;
