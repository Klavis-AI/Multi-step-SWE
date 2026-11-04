-- json1.html "The json_remove() function": "The json_remove(X,P,...) function
-- returns a new JSON value that is the X with all the elements identified by
-- path arguments removed.  Paths that select elements not found in X are
-- silently ignored. ... If the json_remove(X) function is called with no path
-- arguments, then it returns the input X reformatted, with excess whitespace
-- removed."  The seven lines below are that section's own worked examples.
SELECT json_remove('[0,1,2,3,4]','$[2]');
SELECT json_remove('[0,1,2,3,4]','$[2]','$[0]');
SELECT json_remove('[0,1,2,3,4]','$[0]','$[2]');
SELECT json_remove('{"x":25,"y":42}');
SELECT json_remove('{"x":25,"y":42}','$.z');
SELECT json_remove('{"x":25,"y":42}','$.y');
SELECT typeof(json_remove('{"x":25,"y":42}','$'));
