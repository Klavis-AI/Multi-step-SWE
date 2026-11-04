-- lang_corefunc.html: "The %n format is silently ignored and does not consume
-- an argument. The %p format is an alias for %X. The %z format is
-- interchangeable with %s."
SELECT printf('a%nb', 'unused'), printf('%n%d', 5);
SELECT printf('%p', 255), printf('%p|%X', 255, 255);
SELECT printf('%z', 'hi'), printf('[%z][%s]', 'a', 'b');
