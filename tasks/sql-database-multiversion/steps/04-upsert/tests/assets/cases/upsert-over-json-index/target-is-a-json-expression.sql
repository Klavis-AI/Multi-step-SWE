-- PROFILE.md 4.2: a conflict target may name an expression built from the
-- JSON1 functions of step 3 section 3.1, when a UNIQUE index has that
-- expression as its key.
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT);
CREATE UNIQUE INDEX doc_k ON doc(json_extract(body,'$.k'));
INSERT INTO doc VALUES(1,'{"k":"a","v":1}'),(2,'{"k":"b","v":2}');
-- a document whose $.k already exists collides on the indexed expression
INSERT INTO doc VALUES(3,'{"k":"a","v":9}')
  ON CONFLICT(json_extract(body,'$.k')) DO UPDATE SET body=excluded.body;
SELECT id,body FROM doc ORDER BY id;
SELECT count(*) FROM doc;
-- one with a new $.k is inserted
INSERT INTO doc VALUES(4,'{"k":"c","v":3}')
  ON CONFLICT(json_extract(body,'$.k')) DO UPDATE SET body=excluded.body;
SELECT id,json_extract(body,'$.k'),json_extract(body,'$.v') FROM doc ORDER BY id;
-- DO NOTHING with the same target
INSERT INTO doc VALUES(5,'{"k":"c","v":99}')
  ON CONFLICT(json_extract(body,'$.k')) DO NOTHING;
SELECT id,json_extract(body,'$.k'),json_extract(body,'$.v') FROM doc ORDER BY id;
SELECT count(*) FROM doc;
PRAGMA integrity_check;
