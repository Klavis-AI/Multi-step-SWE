SELECT key, value FROM jsonb_each('{"x":"1","y":2}'::jsonb) ORDER BY key;
SELECT key, value FROM jsonb_each_text('{"x":"1","y":2}'::jsonb) ORDER BY key;
SELECT jsonb_object_keys('{"b":1,"a":2}'::jsonb) AS k ORDER BY k;
SELECT * FROM jsonb_array_elements('[1,"a",null]'::jsonb) AS e;
