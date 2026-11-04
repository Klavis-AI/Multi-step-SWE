SELECT * FROM JSON_TABLE('[10,20,30]'::jsonb, '$[*]'
                         COLUMNS (n FOR ORDINALITY, v int PATH '$')) ORDER BY n;
SELECT n FROM JSON_TABLE('["a","b"]'::jsonb, '$[*]' COLUMNS (n FOR ORDINALITY)) ORDER BY n;
SELECT pg_typeof(n) FROM JSON_TABLE('[1]'::jsonb, '$[*]' COLUMNS (n FOR ORDINALITY));
