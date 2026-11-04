SELECT '{"a":1}'::jsonb @@ '$.a';
SELECT '{"a":1}'::jsonb @? '$.a == 1';
SELECT jsonb_path_match('{"a":1}'::jsonb, '$.a');
SELECT '{"a":1}'::json @? '$.a';
SELECT '{"a":1}'::jsonb @? 'bogus path';
