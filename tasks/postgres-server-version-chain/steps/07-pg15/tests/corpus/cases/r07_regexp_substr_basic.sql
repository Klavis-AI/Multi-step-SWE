SELECT regexp_substr('abcabc','b.');
SELECT regexp_substr('abcabc','b.',4);
SELECT regexp_substr('abcabc','b.',1,2);
SELECT regexp_substr('abcabc','z') IS NULL;
SELECT regexp_substr('abc','(b)(c)',1,1,'',2);
