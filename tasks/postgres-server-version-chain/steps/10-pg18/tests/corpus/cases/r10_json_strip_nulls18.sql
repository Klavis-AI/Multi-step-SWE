SELECT jsonb_strip_nulls('{"a":null,"b":[1,null]}'::jsonb);
SELECT jsonb_strip_nulls('{"a":null,"b":[1,null]}'::jsonb, true);
SELECT jsonb_strip_nulls('{"a":null,"b":[1,null]}'::jsonb, false);
SELECT json_strip_nulls('{"a":null,"b":[1,null]}'::json, true);
