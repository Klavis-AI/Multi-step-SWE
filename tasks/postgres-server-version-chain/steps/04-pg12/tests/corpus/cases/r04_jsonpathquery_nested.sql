SELECT jsonb_path_query_array('{"a":{"b":{"c":[1,2]}}}'::jsonb, '$.a.b.c[*]');
SELECT jsonb_path_query_array('{"a":{"b":{"c":[1,2]}}}'::jsonb, '$.**.c[*]');
SELECT jsonb_path_query_array('[{"x":1},{"x":2}]'::jsonb, '$[*].x');
SELECT jsonb_path_query_array('{"a":[1,2,3,4]}'::jsonb, '$.a[1 to 2]');
SELECT jsonb_path_query_array('{"a":[1,2,3,4]}'::jsonb, '$.a[last]');
