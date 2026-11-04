SELECT * FROM JSON_TABLE('[{"a":"zz"}]'::jsonb, '$[*]'
       COLUMNS (a int PATH '$.a' DEFAULT -1 ON ERROR)) ORDER BY a;
SELECT * FROM JSON_TABLE('[{"a":"zz"}]'::jsonb, '$[*]'
       COLUMNS (a int PATH '$.a' NULL ON ERROR)) ORDER BY a NULLS LAST;
SELECT * FROM JSON_TABLE('[{"a":"zz"}]'::jsonb, '$[*]'
       COLUMNS (a int PATH '$.a' ERROR ON ERROR));
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, 'strict $.nope' COLUMNS (x int PATH '$') EMPTY ON ERROR);
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, 'strict $.nope' COLUMNS (x int PATH '$') ERROR ON ERROR);
