SELECT regexp_like('abc','b'), regexp_like('abc','z');
SELECT regexp_like('abc','B','i'), regexp_like('abc','B');
SELECT regexp_like('a'||chr(10)||'b','a.b'), regexp_like('a'||chr(10)||'b','a.b','n');
SELECT regexp_like(NULL,'b') IS NULL, regexp_like('abc',NULL) IS NULL;
