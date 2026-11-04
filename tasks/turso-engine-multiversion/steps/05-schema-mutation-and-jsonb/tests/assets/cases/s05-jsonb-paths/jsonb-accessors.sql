-- R05.12: the JSON path accessors take a JSONB argument directly and give the
-- same answers they give for the equivalent JSON text.
--
-- json_extract, jsonb_extract, json_type and json_array_length are each called
-- on a binary document -- the blob jsonb() returns, with no json() conversion
-- anywhere in the expression -- through object keys, array subscripts and a
-- nested path. The two jsonb_extract statements are the same extraction from
-- the same document in its two representations, binary first and then text,
-- and both must answer 9. That pair is what "and give the same answers they
-- give for the equivalent JSON text" asserts, for the one accessor whose name
-- promises the binary form specifically.
SELECT json_extract(jsonb('{"a":{"b":7}}'),'$.a.b');
SELECT jsonb_extract(jsonb('{"a":9,"b":[4,5]}'),'$.a');
SELECT jsonb_extract('{"a":9,"b":[4,5]}','$.a');
SELECT json_type(jsonb('{"a":[1]}')), json_type(jsonb('{"a":[1]}'),'$.a');
SELECT json_type(jsonb('[1,"s",null]'),'$[0]'), json_type(jsonb('[1,"s",null]'),'$[1]');
SELECT json_array_length(jsonb('[1,2,3]')), json_array_length(jsonb('[]'));
SELECT json_extract(jsonb('[10,20,30]'),'$[2]'), json_extract(jsonb('{"a":[1,{"b":2}]}'),'$.a[1].b');
