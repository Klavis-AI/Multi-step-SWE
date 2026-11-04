-- R02.10 (and R02.9's well-formed half): JSON functions given non-text
-- arguments -- integers, reals, NULL and JSON
-- literals -- must coerce exactly the way SQLite coerces them: a NULL
-- argument propagates NULL, a number is emitted as a JSON number and never
-- as a quoted string, and the JSON literals true/false/null keep their own
-- types rather than collapsing to text.
SELECT json_object('a',1,'b',2.5,'c',NULL,'d','s');
SELECT json_array(1,2.5,NULL,'s');
SELECT json_type(NULL) IS NULL, json_valid(NULL) IS NULL;
SELECT json_valid(1), json_valid(2.5);
SELECT json('1'), json('null'), json('true'), json('"s"');
SELECT json_type('true'), json_type('false'), json_type('null'), json_type('1'), json_type('1.5'), json_type('"s"');
SELECT json_extract('{"a":true,"b":false,"c":1.5}','$.a'), json_extract('{"a":true,"b":false,"c":1.5}','$.c');
SELECT json_array_length('[]'), json_array_length('[[1,2]]','$[0]');
