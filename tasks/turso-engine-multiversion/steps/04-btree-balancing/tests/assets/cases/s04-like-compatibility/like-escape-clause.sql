-- R04.6: LIKE with an ESCAPE clause, including an escaped wildcard matched
-- literally and the escape character escaping itself.
--
-- With an ESCAPE clause the escaped character is matched literally and loses
-- its wildcard meaning; an unescaped wildcard keeps it. Every answer below is
-- the one stock sqlite3 3.50.2 gives for the same predicate.
SELECT 'a_c' LIKE 'a\_c' ESCAPE '\', 'abc' LIKE 'a\_c' ESCAPE '\';
SELECT 'a%c' LIKE 'a\%c' ESCAPE '\', '100%' LIKE '100\%' ESCAPE '\';
SELECT 'axxc' LIKE 'a%c', 'a' LIKE 'a' ESCAPE '\', 'a\c' LIKE 'a\\c' ESCAPE '\';
