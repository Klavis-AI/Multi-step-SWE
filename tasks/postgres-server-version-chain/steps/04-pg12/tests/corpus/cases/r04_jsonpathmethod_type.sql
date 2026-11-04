SELECT jsonb_path_query('1'::jsonb, '$.type()');
SELECT jsonb_path_query('"s"'::jsonb, '$.type()');
SELECT jsonb_path_query('true'::jsonb, '$.type()');
SELECT jsonb_path_query('null'::jsonb, '$.type()');
SELECT jsonb_path_query('[1]'::jsonb, '$.type()');
SELECT jsonb_path_query('{"a":1}'::jsonb, '$.type()');
