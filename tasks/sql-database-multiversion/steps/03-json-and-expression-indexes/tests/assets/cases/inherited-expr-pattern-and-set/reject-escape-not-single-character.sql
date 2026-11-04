-- lang_expr.html, "The LIKE and GLOB operators": "If the optional ESCAPE
-- clause is present, then the expression following the ESCAPE keyword must
-- evaluate to a string consisting of a single character."  PROFILE.md
-- section 3 puts LIKE and its ESCAPE clause in scope with that page, and
-- section 4 records that like(Y,X,Z) is the function spelling of
-- "X LIKE Y ESCAPE Z", so the same rule governs its third argument.
CREATE TABLE t(a TEXT);
INSERT INTO t VALUES('100%');
-- a single-character escape is the permitted form
SELECT a FROM t WHERE a LIKE '100\%' ESCAPE '\';
SELECT 'a_b' LIKE 'a\_b' ESCAPE '\', 'axb' LIKE 'a\_b' ESCAPE '\';
-- an escape expression of two characters is not a string of one character
SELECT 'x' LIKE 'x' ESCAPE 'ab';
SELECT like('x', 'x', 'ab');
SELECT a FROM t;
