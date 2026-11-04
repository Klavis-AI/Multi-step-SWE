SELECT * FROM JSON_TABLE('[{"a":1,"b":"x"},{"a":2,"b":"y"}]'::jsonb, '$[*]'
                         COLUMNS (a int PATH '$.a', b text PATH '$.b')) ORDER BY a;
SELECT a FROM JSON_TABLE('{"a":7}'::jsonb, '$' COLUMNS (a int PATH '$.a')) ORDER BY a;
SELECT count(*) FROM JSON_TABLE('[]'::jsonb, '$[*]' COLUMNS (a int PATH '$.a'));
