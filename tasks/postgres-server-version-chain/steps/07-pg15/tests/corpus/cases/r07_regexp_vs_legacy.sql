SELECT regexp_substr('abcabc','b.') = substring('abcabc' from 'b.');
SELECT regexp_like('abcabc','b.') = ('abcabc' ~ 'b.');
SELECT regexp_replace('abcabc','b','X','g');
SELECT regexp_replace('abcabc','b','X',1,0);
SELECT regexp_matches('abcabc','b.') AS m ORDER BY m;
