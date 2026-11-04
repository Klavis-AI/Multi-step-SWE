-- json1.html 3.5: "The json_insert(), json_replace(), and json_set()
-- functions always take an odd number of arguments", so an even number is not
-- a call these functions accept.  3.6: json_object() "accepts zero or more
-- pairs of arguments", so an odd number is not a call it accepts.
SELECT json_set('{"a":1}','$.a',2);
SELECT json_set('{"a":1}','$.a');
SELECT json_insert('{"a":1}','$.b');
SELECT json_replace('{"a":1}','$.a');
SELECT json_set('{"a":1}','$.a',2,'$.b');
SELECT json_object('a');
SELECT json_object('a',1,'b');
SELECT json_insert('{"a":1}');
