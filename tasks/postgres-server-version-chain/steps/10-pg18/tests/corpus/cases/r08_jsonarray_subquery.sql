SELECT JSON_ARRAY(SELECT g FROM generate_series(1,4) g);
SELECT JSON_ARRAY(SELECT g FROM generate_series(1,3) g RETURNING jsonb);
SELECT JSON_ARRAY(SELECT g FROM generate_series(1,0) g);
SELECT JSON_ARRAY(SELECT NULL::int);
SELECT JSON_ARRAY(SELECT g, g FROM generate_series(1,2) g);
