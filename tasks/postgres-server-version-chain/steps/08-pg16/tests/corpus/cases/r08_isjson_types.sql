SELECT '{"a":1}'::json IS JSON, '{"a":1}'::jsonb IS JSON;
SELECT '{"a":1}'::jsonb IS JSON OBJECT, '[1]'::jsonb IS JSON ARRAY;
SELECT '{"a":1}'::varchar IS JSON;
SELECT '\x7b7d'::bytea IS JSON;
SELECT 1::int IS JSON;
