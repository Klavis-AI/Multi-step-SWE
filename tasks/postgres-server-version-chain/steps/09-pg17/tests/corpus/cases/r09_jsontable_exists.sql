SELECT * FROM JSON_TABLE('[{"a":1},{"c":3}]'::jsonb, '$[*]'
       COLUMNS (has_a bool EXISTS PATH '$.a', a int PATH '$.a')) ORDER BY has_a;
SELECT * FROM JSON_TABLE('[{"a":1}]'::jsonb, '$[*]'
       COLUMNS (e int EXISTS PATH '$.a', f int EXISTS PATH '$.zz')) ORDER BY e;
SELECT * FROM JSON_TABLE('[{"a":1}]'::jsonb, '$[*]'
       COLUMNS (e text EXISTS PATH 'strict $.zz' TRUE ON ERROR)) ORDER BY e;
