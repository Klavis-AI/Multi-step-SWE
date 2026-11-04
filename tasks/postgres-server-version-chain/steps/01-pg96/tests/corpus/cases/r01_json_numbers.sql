SELECT '1.0000'::jsonb, '{"n":1E2}'::jsonb, '1e-3'::jsonb, '-0'::jsonb;
SELECT '{"bb":1,"a":2,"c":3,"aa":4}'::jsonb;
SELECT '{"a":1,"a":2}'::jsonb, '{"a":1,"a":2}'::json;
SELECT '  {"a"  :  1}  '::json, '  {"a"  :  1}  '::jsonb;
SELECT jsonb_array_length('[1,2,3]'::jsonb), jsonb_array_length('[]'::jsonb);
