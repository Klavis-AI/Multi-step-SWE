SELECT regexp_match('abcabc', '(b)');
SELECT regexp_matches('abcabc', '(b)');
SELECT count(*) FROM (SELECT regexp_matches('abcabc', '(b)', 'g')) s;
SELECT count(*) FROM (SELECT regexp_match('abcabc', '(b)')) s;
SELECT regexp_match('abc', '(a)(x)?(c)?');
