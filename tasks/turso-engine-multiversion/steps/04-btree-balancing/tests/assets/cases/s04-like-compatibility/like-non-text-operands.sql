-- R04.7: LIKE applied to an integer, a real, a blob and NULL -- each returns a
-- value rather than raising.
--
-- LIKE applied to a number or a blob converts the operand rather than failing,
-- and a NULL operand on either side yields NULL, which the case reads through
-- typeof() and coalesce() so that it is visible as a value and not as an empty
-- rendering. Matching stays case-insensitive for ASCII. Every answer below is
-- the one stock sqlite3 3.50.2 gives.
SELECT 100 LIKE '1%', 1.5 LIKE '1%', 100 LIKE '2%';
SELECT typeof(NULL LIKE 'a'), typeof('x' LIKE NULL), coalesce(NULL LIKE 'a', -1);
SELECT x'6162' LIKE 'ab', 'ABC' LIKE 'abc', 'abc' LIKE 'ABC';
