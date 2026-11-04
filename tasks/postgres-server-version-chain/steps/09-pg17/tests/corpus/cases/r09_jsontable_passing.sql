SELECT * FROM JSON_TABLE('[{"a":1},{"a":5}]'::jsonb, '$[*] ? (@.a > $lim)'
       PASSING 3 AS lim COLUMNS (a int PATH '$.a')) ORDER BY a;
SELECT * FROM JSON_TABLE('{"a":"x"}'::jsonb, '$ ? (@.a == $s)'
       PASSING 'x' AS s COLUMNS (a text PATH '$.a')) ORDER BY a;
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, '$ ? (@.a > $nosuch)' COLUMNS (a int PATH '$.a'));
