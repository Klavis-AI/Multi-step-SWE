-- json1.html "The json_type() function": "The 'type' returned by json_type()
-- is on of the following an SQL text values: 'null', 'true', 'false',
-- 'integer', 'real', 'text', 'array', or 'object'."  All eight, at the
-- outermost element.
SELECT json_type('null');
SELECT json_type('true');
SELECT json_type('false');
SELECT json_type('12');
SELECT json_type('12.5');
SELECT json_type('"s"');
SELECT json_type('[]');
SELECT json_type('{}');
-- The result is an SQL text value.
SELECT typeof(json_type('[]'));
-- A path that selects nothing gives NULL, not one of the eight names.
SELECT typeof(json_type('{"a":1}','$.zz'));
