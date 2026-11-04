SELECT trim('  ab  ') || '|', ltrim('  ab  ') || '|', rtrim('  ab  ') || '|';
SELECT trim('xxabxx', 'x'), ltrim('xxabxx', 'x'), rtrim('xxabxx', 'x');
SELECT replace('a-b-c', '-', '+'), replace('abc', '', 'X');
SELECT instr('abcde', 'cd'), instr('abcde', 'z'), instr(x'0102030405', x'0304');
SELECT instr(NULL, 'a') IS NULL, instr('a', NULL) IS NULL;
