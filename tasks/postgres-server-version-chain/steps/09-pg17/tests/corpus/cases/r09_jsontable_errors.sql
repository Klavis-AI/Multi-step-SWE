SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, '$' COLUMNS (a int));
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, '$' COLUMNS (a int PATH '$.a', a int PATH '$.a'));
SELECT * FROM JSON_TABLE('not json', '$' COLUMNS (a int PATH '$.a'));
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, '$' COLUMNS (a int PATH '$$bad'));
SELECT * FROM JSON_TABLE('{"a":1}'::jsonb, '$' COLUMNS (a int PATH '$.a') PLAN DEFAULT (UNION));
SELECT count(*) FROM pg_proc WHERE proname = 'json_table';
