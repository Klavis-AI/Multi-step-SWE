SELECT * FROM JSON_TABLE('[{"a":1},{}]'::jsonb, '$[*]'
       COLUMNS (a int PATH '$.a' DEFAULT 99 ON EMPTY)) ORDER BY a;
SELECT * FROM JSON_TABLE('[{"a":1},{}]'::jsonb, '$[*]'
       COLUMNS (a int PATH '$.a' NULL ON EMPTY)) ORDER BY a NULLS LAST;
SELECT * FROM JSON_TABLE('[{}]'::jsonb, '$[*]'
       COLUMNS (a int PATH 'strict $.a' ERROR ON EMPTY));
