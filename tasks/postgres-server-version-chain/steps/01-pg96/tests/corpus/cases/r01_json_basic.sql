SELECT '{"b":1,"a":2}'::json, '{"b":1,"a":2}'::jsonb;
SELECT '{"a":{"b":[1,2]}}'::jsonb -> 'a' -> 'b' ->> 1;
SELECT '{"a":1}'::jsonb || '{"b":2}'::jsonb, '{"a":1,"b":2}'::jsonb - 'a';
SELECT '[1,2,3]'::jsonb -> 1, '[1,2,3]'::jsonb ->> 1, '[1,2,3]'::jsonb #> '{1}';
SELECT jsonb_build_object('k',1,'l',ARRAY[1,2]), json_build_array(1,'a',NULL);
SELECT jsonb_pretty('{"a":[1,2]}'::jsonb);
SELECT jsonb_typeof('1'::jsonb), jsonb_typeof('"x"'::jsonb), jsonb_typeof('[]'::jsonb), jsonb_typeof('{}'::jsonb), jsonb_typeof('null'::jsonb);
