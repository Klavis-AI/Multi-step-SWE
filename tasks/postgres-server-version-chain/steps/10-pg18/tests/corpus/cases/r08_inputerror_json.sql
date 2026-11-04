SELECT message, detail, sql_error_code FROM pg_input_error_info('{"a":}','jsonb');
SELECT message, sql_error_code FROM pg_input_error_info('[1,','json');
SELECT message, sql_error_code FROM pg_input_error_info('nope','bool');
SELECT message, sql_error_code FROM pg_input_error_info('2020-02-30','date');
