-- json1.html 2.0 "Interface Overview": "The json1 extension (currently)
-- stores JSON as ordinary text."  A JSON document is therefore an ordinary
-- TEXT value in an ordinary column, and every step-1 rule about TEXT applies
-- to it (PROFILE.md 3.1).
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT);
INSERT INTO doc VALUES
  (1,'{"name":"ann","age":31,"tags":["x","y"]}'),
  (2,'{"name":"bob","age":25,"tags":[]}'),
  (3,'{"name":"cid","age":40,"tags":["z"]}');
SELECT typeof(body) FROM doc WHERE id = 1;
SELECT count(*) FROM doc WHERE json_valid(body) = 1;
-- 3.4: a single path gives a value of the JSON element's own SQL datatype,
-- so the extracted age sorts and compares as an INTEGER.
SELECT id, json_extract(body,'$.name'), json_extract(body,'$.age')
  FROM doc ORDER BY id;
SELECT id FROM doc WHERE json_extract(body,'$.age') > 30 ORDER BY id;
SELECT json_extract(body,'$.name') FROM doc
  ORDER BY json_extract(body,'$.age') DESC;
SELECT sum(json_extract(body,'$.age')), max(json_extract(body,'$.age')) FROM doc;
-- 3.3 over a column value
SELECT id, json_array_length(body,'$.tags') FROM doc ORDER BY id;
SELECT id, json_type(body,'$.tags') FROM doc ORDER BY id;
-- 3.6 building a new document out of column values.  The value arguments are
-- an ordinary INTEGER column and an INTEGER json1 result, so 2.3's TEXT rule
-- does not bear on them; PROFILE.md 3.7 keeps out of scope what that rule
-- means for a json_extract() result used as a value argument.
SELECT json_object('id',id,'n',json_array_length(body,'$.tags'))
  FROM doc ORDER BY id;
