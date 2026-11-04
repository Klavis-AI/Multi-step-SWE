-- lang_corefunc.html: "The printf(FORMAT,...) SQL function works like the
-- sqlite3_mprintf() C-language function and the printf() function from the
-- standard C library."  PROFILE.md section 2.3 lists the conversions in scope.
SELECT printf('%d', 42), printf('%i', -7), printf('%s', 'abc');
SELECT printf('%o|%x|%X', 64, 255, 255);
SELECT printf('%d-%s-%X', 1, 'two', 255);
SELECT printf('%%'), printf('100%%'), printf('%%d');
SELECT printf('a%db%sc', 1, 'z');
