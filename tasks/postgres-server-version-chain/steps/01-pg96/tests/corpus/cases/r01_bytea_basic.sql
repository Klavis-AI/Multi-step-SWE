SELECT 'abc'::bytea, '\\x00ff41'::bytea, length('\\x00ff41'::bytea);
SELECT '\\x0102'::bytea || '\\x03'::bytea;
SELECT encode('\\x616263'::bytea,'hex'), encode('abc'::bytea,'base64'), decode('YWJj','base64');
SELECT md5('\\x616263'::bytea);
