CREATE TABLE q4_joi(id int, doc jsonb);
INSERT INTO q4_joi VALUES (1,'{"a":1,"t":["x"]}'),(2,'{"a":9}'),(3,'{"b":2}');
CREATE INDEX q4_joi_g ON q4_joi USING gin (doc);
SELECT id FROM q4_joi WHERE doc @? '$.a' ORDER BY id;
SELECT id FROM q4_joi WHERE doc @@ '$.a > 5' ORDER BY id;
SELECT id FROM q4_joi WHERE doc @> '{"a":1}' ORDER BY id;
SELECT amname FROM pg_am a JOIN pg_class c ON c.relam = a.oid WHERE c.relname = 'q4_joi_g';
DROP TABLE q4_joi;
