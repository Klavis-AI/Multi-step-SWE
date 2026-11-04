SELECT 'abcabc' ~ 'b.c', 'abcabc' ~ '^b', 'ABC' ~* 'abc', 'ABC' !~ 'abc';
SELECT regexp_replace('abcabc','b','X'), regexp_replace('abcabc','b','X','g');
SELECT regexp_matches('foobarbaz','(b..)(b..)');
SELECT regexp_split_to_array('a1b22c','[0-9]+');
SELECT substring('abcdef' from 'c.e');
