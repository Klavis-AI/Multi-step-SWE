SELECT jsonb_path_query_array('{"a":1}'::jsonb, '$.a[*]');
SELECT jsonb_path_query_array('{"a":1}'::jsonb, 'strict $.a[*]');
SELECT jsonb_path_query_array('[1,2]'::jsonb, '$.a');
SELECT jsonb_path_query_array('{}'::jsonb, '$.a.b.c');
SELECT jsonb_path_query_array('[[1,2],[3]]'::jsonb, 'lax $[*][*]');
