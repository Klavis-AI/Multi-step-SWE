SELECT JSON_QUERY(jsonb '{"a":"x"}', '$.a' KEEP QUOTES);
SELECT JSON_QUERY(jsonb '{"a":"x"}', '$.a' OMIT QUOTES);
SELECT JSON_QUERY(jsonb '{"a":"x"}', '$.a' RETURNING text OMIT QUOTES);
SELECT JSON_QUERY(jsonb '{"a":1}', '$.b' DEFAULT '"d"'::jsonb ON EMPTY);
SELECT JSON_QUERY(jsonb '{"a":1}', '$.b' EMPTY ARRAY ON EMPTY);
