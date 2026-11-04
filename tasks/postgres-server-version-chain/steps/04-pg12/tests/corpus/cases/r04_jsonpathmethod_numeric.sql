SELECT jsonb_path_query('-2.5'::jsonb, '$.abs()');
SELECT jsonb_path_query('2.5'::jsonb, '$.ceiling()');
SELECT jsonb_path_query('2.5'::jsonb, '$.floor()');
SELECT jsonb_path_query('"3.5"'::jsonb, '$.double()');
SELECT jsonb_path_query_array('[1,2,3]'::jsonb, '$[*].abs()');
