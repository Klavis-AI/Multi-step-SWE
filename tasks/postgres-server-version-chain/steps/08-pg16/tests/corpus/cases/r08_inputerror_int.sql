SELECT * FROM pg_input_error_info('x','int');
SELECT message, sql_error_code FROM pg_input_error_info('99999999999','int4');
SELECT message, sql_error_code FROM pg_input_error_info('12345','numeric(4,2)');
SELECT message IS NULL, sql_error_code IS NULL FROM pg_input_error_info('42','int');
