SELECT ('{"a":{"b":[1,2]}}'::jsonb)['a'];
SELECT ('{"a":{"b":[1,2]}}'::jsonb)['a']['b'];
SELECT ('{"a":{"b":[1,2]}}'::jsonb)['a']['b'][0];
SELECT ('[1,2,3]'::jsonb)[0], ('[1,2,3]'::jsonb)[2];
SELECT ('{"a":1}'::jsonb)['nosuch'] IS NULL;
