-- json1.html 3.9 "The json_valid() function": "The json_valid(X) function
-- return 1 if the argument X is well-formed JSON and return 0 if the
-- argument X is not well-formed JSON."  The first two lines are that
-- section's own worked examples.
SELECT json_valid('{"x":35}');
SELECT json_valid('{"x":35');
SELECT json_valid('[1,2,3]'), json_valid('[1,2,');
SELECT json_valid('"str"'), json_valid('str');
SELECT json_valid('null'), json_valid('true'), json_valid('false'), json_valid('12');
-- 2.1: whitespace around a JSON input is ignored when deciding validity.
SELECT json_valid('  {"a":1}  ');
-- 2.1: "These routines accept exactly the rfc-7159 JSON syntax - no more and
-- no less", so an unquoted object label and a trailing comma are not JSON.
SELECT json_valid('{a:1}'), json_valid('[1,]');
SELECT json_valid('');
-- 2.1 makes json_valid() the one function that answers rather than throwing.
SELECT typeof(json_valid('[]'));
