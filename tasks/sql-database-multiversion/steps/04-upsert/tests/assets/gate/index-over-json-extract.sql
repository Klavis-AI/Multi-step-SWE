-- The canonical 3.9.0 combination: expridx.html allows a deterministic
-- function call in an index key, and json_extract() is one, so a column of
-- JSON documents can be indexed on a field inside them.  What the verifier
-- observes is only that the answers are the ones the same queries give
-- without the index -- which index the planner picks is never scored
-- (TEST_INTERFACE.md 6).
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT);
INSERT INTO doc VALUES
  (1,'{"name":"ann","age":31}'),
  (2,'{"name":"bob","age":25}'),
  (3,'{"name":"cid","age":40}');
SELECT id FROM doc WHERE json_extract(body,'$.age') = 25;
SELECT id FROM doc ORDER BY json_extract(body,'$.age');
CREATE INDEX doc_age ON doc(json_extract(body,'$.age'));
CREATE INDEX doc_name ON doc(json_extract(body,'$.name'));
SELECT id FROM doc WHERE json_extract(body,'$.age') = 25;
SELECT id FROM doc ORDER BY json_extract(body,'$.age');
SELECT id FROM doc WHERE json_extract(body,'$.name') = 'cid';
SELECT id FROM doc WHERE json_extract(body,'$.age') > 30 ORDER BY id;
SELECT sql FROM sqlite_master WHERE name='doc_age';
-- editing the document through json_set() keeps the index in step
UPDATE doc SET body = json_set(body,'$.age',26) WHERE id = 2;
SELECT id FROM doc WHERE json_extract(body,'$.age') = 26;
SELECT id FROM doc WHERE json_extract(body,'$.age') = 25;
INSERT INTO doc VALUES(4,'{"name":"dot","age":19}');
SELECT id FROM doc ORDER BY json_extract(body,'$.age');
DELETE FROM doc WHERE id = 3;
SELECT id FROM doc WHERE json_extract(body,'$.age') = 40;
SELECT id, json_extract(body,'$.age') FROM doc ORDER BY id;
PRAGMA integrity_check;
-- dropping the index changes no answer
DROP INDEX doc_age;
SELECT id FROM doc ORDER BY json_extract(body,'$.age');
