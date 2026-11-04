-- R04.4: CAST from text to INTEGER, REAL and NUMERIC over whitespace, a
-- trailing non-numeric tail, an exponent and a truncated fraction.
-- R04.5: text with no numeric prefix at all yielding 0 rather than raising.
--
-- A text value converts on its longest numeric prefix: surrounding blanks are
-- skipped, trailing rubbish is ignored, a fraction truncates towards zero, and
-- text with no numeric prefix at all is zero rather than an error. Every row
-- below is what stock sqlite3 3.50.2 answers for the same expression.
SELECT CAST('  42  ' AS INTEGER), CAST('42abc' AS INTEGER), CAST('abc' AS INTEGER);
SELECT CAST('3.9' AS INTEGER), CAST('-3.9' AS INTEGER), CAST('  -7.25e2xyz' AS REAL);
SELECT CAST('12abc' AS NUMERIC), CAST('abc' AS NUMERIC), typeof(CAST('abc' AS NUMERIC));
SELECT CAST('abc' AS REAL), CAST('' AS INTEGER), CAST('007' AS INTEGER);
