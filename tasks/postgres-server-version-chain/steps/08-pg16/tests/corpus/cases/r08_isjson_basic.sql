SELECT '1'::json IS JSON, '{'::text IS JSON;
SELECT '{"a":1}'::text IS JSON, '[1,2]'::text IS JSON, 'null'::text IS JSON;
SELECT ''::text IS JSON, ' '::text IS JSON, '  {"a":1}  '::text IS JSON;
SELECT 'tru'::text IS JSON, 'true'::text IS JSON;
