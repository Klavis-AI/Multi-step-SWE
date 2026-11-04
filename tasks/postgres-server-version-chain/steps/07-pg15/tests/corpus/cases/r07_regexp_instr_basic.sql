SELECT regexp_instr('abcabc','b');
SELECT regexp_instr('abcabc','b',1,2);
SELECT regexp_instr('abcabc','b',1,1,1);
SELECT regexp_instr('abcabc','z');
SELECT regexp_instr('ABCabc','b',1,1,0,'i');
