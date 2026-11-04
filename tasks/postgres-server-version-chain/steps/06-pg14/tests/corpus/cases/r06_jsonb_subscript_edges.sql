SELECT ('[1,2,3]'::jsonb)[-1], ('[1,2,3]'::jsonb)[9] IS NULL;
SELECT ('{"1":"x"}'::jsonb)['1'], ('{"1":"x"}'::jsonb)[1];
SELECT ('"scalar"'::jsonb)['a'] IS NULL, ('null'::jsonb)['a'] IS NULL;
SELECT pg_typeof(('{"a":1}'::jsonb)['a']);
SELECT ('{"a":1}'::jsonb)['a'] = '1'::jsonb;
