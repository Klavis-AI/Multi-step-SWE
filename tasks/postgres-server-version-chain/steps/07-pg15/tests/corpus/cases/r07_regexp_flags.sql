SELECT regexp_count('aXbXc','x','1','i');
SELECT regexp_like('AB','ab','c'), regexp_like('AB','ab','i');
SELECT regexp_substr('a b','a\sb','1','','');
SELECT regexp_replace('a.b','.','X',1,0,'q');
SELECT regexp_like('abc','a b c','x');
