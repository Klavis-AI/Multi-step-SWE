CREATE TABLE q4_jqt(id int, doc jsonb);
INSERT INTO q4_jqt VALUES (1,'{"tags":["a","b"],"n":5}'),(2,'{"tags":["c"],"n":50}'),(3,'{"n":1}');
SELECT id FROM q4_jqt WHERE doc @? '$.tags[*]' ORDER BY id;
SELECT id, jsonb_path_query_array(doc, '$.tags[*]') FROM q4_jqt ORDER BY id;
SELECT id FROM q4_jqt WHERE doc @@ '$.n > 4' ORDER BY id;
SELECT id, jsonb_path_query_first(doc, '$.n') FROM q4_jqt ORDER BY id;
DROP TABLE q4_jqt;
