SELECT pg_input_is_valid('1.5','numeric(4,2)'), pg_input_is_valid('12345','numeric(4,2)');
SELECT pg_input_is_valid('abc','varchar(2)'), pg_input_is_valid('ab','varchar(2)');
SELECT pg_input_is_valid('abc','char(2)');
SELECT pg_input_is_valid('9999999999','int4'), pg_input_is_valid('99','int2');
