SELECT jsonb_path_query('"abc"'::jsonb, '$.double()');
SELECT jsonb_path_query('"abc"'::jsonb, '$.abs()');
SELECT jsonb_path_query('{"a":1}'::jsonb, '$.a.nosuchmethod()');
SELECT jsonb_path_query('[1,2]'::jsonb, '$[*] + 10');
SELECT jsonb_path_query('1'::jsonb, '$ + 1');
