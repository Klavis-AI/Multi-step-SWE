SELECT '{"a":1}'::jsonb @> '{"a":1}'::jsonb, '{"a":1}'::jsonb <@ '{"a":1,"b":2}'::jsonb;
SELECT '{"a":1}'::jsonb ? 'a', '{"a":1}'::jsonb ?| ARRAY['b','a'], '{"a":1}'::jsonb ?& ARRAY['a','b'];
SELECT '[1,2]'::jsonb @> '2'::jsonb, '[[1,2]]'::jsonb @> '[[1]]'::jsonb;
SELECT jsonb_set('{"a":1}'::jsonb,'{b}','2'::jsonb), jsonb_insert('{"a":[1,3]}'::jsonb,'{a,1}','2'::jsonb);
SELECT jsonb_strip_nulls('{"a":null,"b":1}'::jsonb);
SELECT jsonb_extract_path('{"a":{"b":1}}'::jsonb,'a','b'), jsonb_extract_path_text('{"a":{"b":1}}'::jsonb,'a','b');
