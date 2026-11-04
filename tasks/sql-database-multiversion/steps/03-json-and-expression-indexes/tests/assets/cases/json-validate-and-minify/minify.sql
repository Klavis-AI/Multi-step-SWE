-- json1.html 3.1 "The json() function": "The json(X) function verifies that
-- its argument X is a valid JSON string and returns a minified version of
-- that JSON string (with all unnecessary whitespace removed)."  The first
-- line below is that section's own worked example.
SELECT json(' { "this" : "is", "a": [ "test" ] } ');
-- json1.html 2.1: "leading and trailing whitespace on JSON inputs is
-- ignored.  Interior whitespace is also ignored, in accordance with the JSON
-- spec."
SELECT json('   [1,   2,3 ]   ');
SELECT json('[ {"k" : [ 1 , 2 ] } , 3 ]');
SELECT json('{ }');
SELECT json('[ ]');
-- Minifying removes whitespace and nothing else: the members of an object
-- and the elements of an array come back in the order they were written
-- (PROFILE.md 3.1).
SELECT json('{"b":1,"a":2}');
SELECT json('[3,1,2]');
-- json() returns SQL text.
SELECT typeof(json('[1]'));
