-- json1.html 3.2 "The json_array() function": "accepts zero or more
-- arguments and returns a well-formed JSON array that is composed from those
-- arguments".  The five lines below are that section's own worked examples.
SELECT json_array(1,2,'3',4);
SELECT json_array('[1,2]');
SELECT json_array(json_array(1,2));
SELECT json_array(1,null,'3','[4,5]','{"six":7.7}');
SELECT json_array(1,null,'3',json('[4,5]'),json('{"six":7.7}'));
-- "zero or more arguments"
SELECT json_array();
SELECT json_array(2.5,-7);
-- A REAL with a zero fractional part keeps its point: PROFILE.md 3.1 writes a
-- number inside a document exactly as TEST_INTERFACE.md 2.3 writes it in a
-- result column, "so json_array(2.0) is [2.0], not [2]".
SELECT json_array(2.0);
SELECT typeof(json_array(1));
