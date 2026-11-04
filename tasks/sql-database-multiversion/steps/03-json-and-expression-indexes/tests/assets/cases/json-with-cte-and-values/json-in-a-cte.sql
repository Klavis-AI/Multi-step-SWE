-- The JSON functions are ordinary scalar SQL functions, so they may be used
-- wherever step 1 section 3 allows an expression -- including inside the body
-- of a common table expression and in the statement that reads it
-- (PROFILE.md 2.1, 3.1).
CREATE TABLE doc(id INTEGER PRIMARY KEY, body TEXT);
INSERT INTO doc VALUES
  (1,'{"name":"ann","age":31,"t":["x","y"]}'),
  (2,'{"name":"bob","age":25,"t":[]}'),
  (3,'{"name":"cid","age":40,"t":["z"]}');
WITH p(id,nm,age) AS (
  SELECT id, json_extract(body,'$.name'), json_extract(body,'$.age') FROM doc
)
SELECT id, nm, age FROM p WHERE age >= 31 ORDER BY id;
WITH p(nm,age) AS (
  SELECT json_extract(body,'$.name'), json_extract(body,'$.age') FROM doc
)
SELECT count(*), min(age), max(age) FROM p;
-- a common table expression whose body builds JSON, read back by json_extract.
-- Both value arguments are INTEGERs -- one an ordinary column, one a json1
-- result -- so 2.3's rule about a TEXT value that "come directly from the
-- result of another json1 function" does not bear on them (PROFILE.md 3.7).
WITH q(j) AS (SELECT json_object('id',id,'n',json_array_length(body,'$.t')) FROM doc)
SELECT j, json_extract(j,'$.n') FROM q ORDER BY json_extract(j,'$.id');
-- two common table expressions, the second reading the first
WITH a(id,age) AS (SELECT id, json_extract(body,'$.age') FROM doc),
     b(id,band) AS (SELECT id, CASE WHEN age < 30 THEN 'young' ELSE 'older' END FROM a)
SELECT id, band FROM b ORDER BY id;
-- a JSON value built inside a CTE and written by a WITH-prefixed INSERT
CREATE TABLE out(id INTEGER PRIMARY KEY, j TEXT);
WITH r(id,j) AS (SELECT id, json_array(id, json_array_length(body,'$.t')) FROM doc WHERE id <= 2)
INSERT INTO out(id,j) SELECT id, j FROM r;
SELECT id, j FROM out ORDER BY id;
