SELECT jsonb_path_query('{"a":1}'::jsonb, 'strict $.b');
SELECT jsonb_path_query('{"a":1}'::jsonb, 'strict $.b', silent => true);
SELECT jsonb_path_query('[1,2]'::jsonb, 'strict $[3]');
SELECT jsonb_path_query_array('[1,2]'::jsonb, 'strict $[3]', silent => true);
SELECT jsonb_path_exists('{"a":1}'::jsonb, 'strict $.b');
SELECT jsonb_path_exists('{"a":1}'::jsonb, 'strict $.b', silent => true);
