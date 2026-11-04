SELECT regexp_instr('abcabc','(b)(c)',1,1,0,'',1);
SELECT regexp_instr('abcabc','(b)(c)',1,1,0,'',2);
SELECT regexp_substr('2020-05-06','(\d+)-(\d+)-(\d+)',1,1,'',3);
SELECT regexp_count('a1b22c333','[0-9]+');
SELECT regexp_replace('a1b22c333','[0-9]+','#',1,2);
