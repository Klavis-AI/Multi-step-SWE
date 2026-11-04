CREATE DOMAIN r08d1 AS int CHECK (VALUE > 0);
SELECT pg_input_is_valid('5','r08d1'), pg_input_is_valid('-5','r08d1');
SELECT message, sql_error_code FROM pg_input_error_info('-5','r08d1');
SELECT message, sql_error_code FROM pg_input_error_info('zz','r08d1');
DROP DOMAIN r08d1;
