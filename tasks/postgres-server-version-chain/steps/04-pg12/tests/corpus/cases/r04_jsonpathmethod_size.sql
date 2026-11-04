SELECT jsonb_path_query('{"a":[1,2,3]}'::jsonb, '$.a.size()');
SELECT jsonb_path_query('{"a":[]}'::jsonb, '$.a.size()');
SELECT jsonb_path_query('{"a":1}'::jsonb, '$.a.size()');
SELECT jsonb_path_query_array('[[1,2],[3]]'::jsonb, 'strict $[*].size()');
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a ? (@.size() == 3)');
