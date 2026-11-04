-- json1.html "The json_type() function": "The json_type(X) function returns
-- the 'type' of the outermost element of X.  The json_type(X,P) function
-- returns the 'type' of the element in X that is selected by path P. ...
-- If the path P in json_type(X,P) selects a element that does not exist in X,
-- then this function returns NULL."  The ten lines below are that section's
-- own worked examples.
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[0]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[1]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[2]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[3]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[4]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[5]');
SELECT json_type('{"a":[2,3.5,true,false,null,"x"]}','$.a[6]');
