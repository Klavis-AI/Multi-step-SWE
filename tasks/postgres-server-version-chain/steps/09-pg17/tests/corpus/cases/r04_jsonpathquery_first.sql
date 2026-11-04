SELECT jsonb_path_query_first('[3,1,2]'::jsonb, '$[*]');
SELECT jsonb_path_query_first('[3,1,2]'::jsonb, '$[*] ? (@ < 2)');
SELECT jsonb_path_query_first('[3,1,2]'::jsonb, '$[*] ? (@ > 9)') IS NULL AS none;
SELECT jsonb_path_query_first('{"a":"str"}'::jsonb, '$.a');
SELECT jsonb_path_query_first('{"a":null}'::jsonb, '$.a');
