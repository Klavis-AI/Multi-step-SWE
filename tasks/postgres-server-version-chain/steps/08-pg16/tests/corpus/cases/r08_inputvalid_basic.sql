SELECT pg_input_is_valid('42','int'), pg_input_is_valid('x','int');
SELECT pg_input_is_valid('2020-01-02','date'), pg_input_is_valid('2020-13-02','date');
SELECT pg_input_is_valid('{"a":1}','jsonb'), pg_input_is_valid('{','jsonb');
SELECT pg_input_is_valid('1,2','point'), pg_input_is_valid('(1,2)','point');
