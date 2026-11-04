SELECT jsonb_path_query_array('[1,2]'::jsonb, '$[*] ? (@ > "x")');
SELECT jsonb_path_query_array('["a"]'::jsonb, '$[*] ? (@ like_regex "[")');
SELECT jsonb_path_query_array('[1]'::jsonb, '$[*] ? (@ starts with 1)');
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*] ? (@ > $min)', '[1,2]');
SELECT jsonb_path_query_array('{"a":[1,2,3]}'::jsonb, '$.a[*] ? (@ > $min)', 'notjson');
