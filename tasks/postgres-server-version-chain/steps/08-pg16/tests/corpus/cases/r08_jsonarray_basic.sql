SELECT JSON_ARRAY(1, 'two', true, 3.5);
SELECT JSON_ARRAY();
SELECT JSON_ARRAY(JSON_ARRAY(1,2), JSON_ARRAY());
SELECT JSON_ARRAY('{"a":1}'::jsonb, '[1]'::json);
SELECT JSON_ARRAY('x'::text, 'x'::varchar(3), 1::int8, 1.5::numeric);
