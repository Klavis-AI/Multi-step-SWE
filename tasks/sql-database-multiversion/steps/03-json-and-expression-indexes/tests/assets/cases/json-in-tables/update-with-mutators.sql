-- The mutators of json1.html 3.5 and json_remove() return a new JSON string,
-- so a stored document is edited by writing that string back with an ordinary
-- step-1 UPDATE.
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT);
INSERT INTO doc VALUES(1,'{"a":1,"c":3}'),(2,'{"a":2,"c":4}');
UPDATE doc SET body = json_set(body,'$.a',99) WHERE id = 1;
SELECT id, body FROM doc ORDER BY id;
UPDATE doc SET body = json_insert(body,'$.e',5);
SELECT id, body FROM doc ORDER BY id;
UPDATE doc SET body = json_remove(body,'$.c') WHERE id = 2;
SELECT id, body FROM doc ORDER BY id;
SELECT changes();
UPDATE doc SET body = json_replace(body,'$.zz',0);
SELECT id, body FROM doc ORDER BY id;
-- The stored text stays well-formed and stays TEXT.
SELECT count(*) FROM doc WHERE json_valid(body) = 1;
SELECT DISTINCT typeof(body) FROM doc;
-- A step-1 transaction rolls a JSON edit back like any other write.
BEGIN;
UPDATE doc SET body = json_set(body,'$.a',-1);
SELECT json_extract(body,'$.a') FROM doc ORDER BY id;
ROLLBACK;
SELECT json_extract(body,'$.a') FROM doc ORDER BY id;
