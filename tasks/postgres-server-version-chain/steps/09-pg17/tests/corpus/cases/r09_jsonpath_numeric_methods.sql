SELECT jsonb_path_query('"123"', '$.bigint()'), jsonb_path_query('"12"', '$.integer()');
SELECT jsonb_path_query('"1.5"', '$.number()'), jsonb_path_query('"1.5"', '$.decimal()');
SELECT jsonb_path_query('1.7', '$.integer()');
SELECT jsonb_path_query('"1.55"', '$.decimal(3,1)');
