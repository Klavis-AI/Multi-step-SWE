-- json1.html 3.2: "If any argument to json_array() is a BLOB then an error is
-- thrown."  3.6 says the same of json_object().  3.5: json_insert(),
-- json_replace() and json_set() "throw an error ... if any argument is a
-- BLOB" -- exercised here for their value arguments; PROFILE.md 3.7 keeps a
-- BLOB in the leading JSON argument out of scope.
SELECT json_array(1,2);
SELECT json_array(x'41');
SELECT json_array(1,x'41');
SELECT json_object('a',x'41');
SELECT json_object(x'41',1);
SELECT json_insert('{}','$.a',x'41');
SELECT json_replace('{"a":1}','$.a',x'41');
SELECT json_set('{}','$.a',x'41');
SELECT json_object('a',1);
