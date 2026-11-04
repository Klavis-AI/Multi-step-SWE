-- A UNIQUE index whose key is a JSON field enforces uniqueness of that field
-- across the table (lang_createindex.html, expridx.html), and a partial one
-- restricts that to the rows its WHERE clause selects (partialindex.html).
CREATE TABLE doc(id INTEGER PRIMARY KEY, kind TEXT, body TEXT);
CREATE UNIQUE INDEX doc_key ON doc(json_extract(body,'$.k'));
INSERT INTO doc VALUES(1,'a','{"k":"one"}'),(2,'a','{"k":"two"}');
SELECT count(id) FROM doc;
-- a second document with the same $.k is rejected and nothing changes
INSERT INTO doc VALUES(3,'b','{"k":"one"}');
SELECT count(id) FROM doc;
SELECT id, json_extract(body,'$.k') FROM doc ORDER BY id;
-- an UPDATE that would collide is rejected the same way
UPDATE doc SET body = json_set(body,'$.k','one') WHERE id = 2;
SELECT id, json_extract(body,'$.k') FROM doc ORDER BY id;
-- a new key is accepted
INSERT INTO doc VALUES(4,'b','{"k":"three"}');
SELECT count(id) FROM doc;
-- a document with no $.k member extracts NULL, and NULLs do not collide
INSERT INTO doc VALUES(5,'b','{"z":1}');
INSERT INTO doc VALUES(6,'b','{"z":2}');
SELECT count(id) FROM doc;
PRAGMA integrity_check;
