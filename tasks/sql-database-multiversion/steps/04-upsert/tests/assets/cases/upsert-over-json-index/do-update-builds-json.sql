-- The DO UPDATE expressions are ordinary expressions, so they may call the
-- JSON1 functions of step 3 section 3.1 over the conflicting row and over
-- excluded (PROFILE.md 4.4 with 3.1).  Step 3 section 3.7 keeps a
-- json_extract() result out of a *value* argument position, so the new field
-- value below comes from an ordinary INTEGER column instead.
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT, v INTEGER);
CREATE UNIQUE INDEX doc_k ON doc(json_extract(body,'$.k'));
INSERT INTO doc VALUES(1,'{"k":"a","v":1}',1);
-- json_set() over the stored document, with the value taken from excluded
INSERT INTO doc VALUES(2,'{"k":"a","v":42}',42)
  ON CONFLICT(json_extract(body,'$.k'))
  DO UPDATE SET body=json_set(doc.body,'$.v',excluded.v), v=excluded.v;
SELECT id,body,v FROM doc ORDER BY id;
-- json_insert() adds a member the stored document did not have
INSERT INTO doc VALUES(3,'{"k":"a","v":7}',7)
  ON CONFLICT(json_extract(body,'$.k'))
  DO UPDATE SET body=json_insert(doc.body,'$.seen',1);
SELECT id,body,v FROM doc ORDER BY id;
-- the trailing WHERE clause may compare extracted values; this one is false
INSERT INTO doc VALUES(4,'{"k":"a","v":5}',5)
  ON CONFLICT(json_extract(body,'$.k'))
  DO UPDATE SET body=excluded.body, v=excluded.v
  WHERE json_extract(excluded.body,'$.v') > json_extract(doc.body,'$.v');
SELECT id,body,v FROM doc ORDER BY id;
-- and this one is true
INSERT INTO doc VALUES(5,'{"k":"a","v":100}',100)
  ON CONFLICT(json_extract(body,'$.k'))
  DO UPDATE SET body=excluded.body, v=excluded.v
  WHERE json_extract(excluded.body,'$.v') > json_extract(doc.body,'$.v');
SELECT id,body,v FROM doc ORDER BY id;
SELECT json_valid(body), typeof(body) FROM doc ORDER BY id;
SELECT count(*) FROM doc;
PRAGMA integrity_check;
