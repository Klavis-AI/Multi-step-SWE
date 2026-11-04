-- json1.html 2.1 "JSON arguments": "For functions that accept JSON as their
-- first argument, that argument can be a JSON object, array, number, string,
-- or null."
SELECT json('{"a":1}');
SELECT json('[1,2]');
SELECT json('12');
SELECT json('12.5');
SELECT json('"a string"');
SELECT json('null');
SELECT json('true');
SELECT json('false');
-- json1.html 2.1: "SQLite numeric values and NULL values are interpreted as
-- JSON numbers and nulls, respectively."  Only the numeric half is scored;
-- PROFILE.md 3.7 keeps a NULL in the leading JSON argument out of scope.
SELECT json(12);
SELECT json(12.5);
SELECT json(-3);
