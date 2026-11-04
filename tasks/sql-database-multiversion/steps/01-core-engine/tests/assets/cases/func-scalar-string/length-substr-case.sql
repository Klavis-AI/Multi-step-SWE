SELECT length('hello'), length(''), length(NULL) IS NULL, length(12345);
SELECT length(x'0102030405');
SELECT substr('abcdef', 2, 3), substr('abcdef', 2), substr('abcdef', -2), substr('abcdef', 4, -2);
SELECT upper('aBc'), lower('AbC');
SELECT hex('abc'), hex(x'0aff');
