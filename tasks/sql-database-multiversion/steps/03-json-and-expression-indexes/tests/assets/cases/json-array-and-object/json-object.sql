-- json1.html 3.6 "The json_object() function": "accepts zero or more pairs
-- of arguments and returns a well-formed JSON object ... The first argument
-- of each pair is the label and the second argument of each pair is the
-- value."  The first three lines are that section's own worked examples and
-- the next three are 2.3's.
SELECT json_object('a',2,'c',4);
SELECT json_object('a',2,'c','{e:5}');
SELECT json_object('a',2,'c',json_object('e',5));
SELECT json_object('ex','[52,3.14159]');
SELECT json_object('ex',json('[52,3.14159]'));
SELECT json_object('ex',json_array(52,3.14159));
-- "zero or more pairs"
SELECT json_object();
-- The pairs appear in the result in the order they were given (PROFILE.md
-- 3.1, from this section's worked examples).
SELECT json_object('a',1,'b',json_array(1,2),'c',json_object('d',3));
-- Same number rule for a value: a zero fractional part is kept.
SELECT json_object('r',2.0);
SELECT typeof(json_object('a',1));
