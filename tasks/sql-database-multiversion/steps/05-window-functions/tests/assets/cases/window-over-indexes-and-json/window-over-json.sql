-- PROFILE.md 5.12: step 3's JSON1 functions keep their meaning, and a window
-- function may take one as an argument, as a PARTITION BY expression and as a
-- window ORDER BY term -- they are ordinary scalar expressions over TEXT
-- (step 3 sections 3.1, 3.2).
CREATE TABLE d(k INTEGER PRIMARY KEY, doc TEXT);
INSERT INTO d VALUES(1,'{"g":"a","n":10}'),
                    (2,'{"g":"a","n":20}'),
                    (3,'{"g":"b","n":30}'),
                    (4,'{"g":"b","n":40}');
SELECT k, json_extract(doc,'$.g') AS g,
       sum(json_extract(doc,'$.n'))
         OVER (PARTITION BY json_extract(doc,'$.g') ORDER BY k) AS running,
       count(*) OVER (PARTITION BY json_extract(doc,'$.g')) AS in_group
  FROM d ORDER BY k;
-- an index on a JSON expression, with the same expression in the window
CREATE INDEX di ON d(json_extract(doc,'$.g'));
SELECT json_extract(doc,'$.g') AS g,
       rank() OVER (ORDER BY json_extract(doc,'$.n')) AS rk
  FROM d ORDER BY k;
-- json_object() over a window result is still just a scalar function call
SELECT k, json_object('k', k, 'rn', row_number() OVER (ORDER BY k)) AS j
  FROM d ORDER BY k;
PRAGMA integrity_check;
