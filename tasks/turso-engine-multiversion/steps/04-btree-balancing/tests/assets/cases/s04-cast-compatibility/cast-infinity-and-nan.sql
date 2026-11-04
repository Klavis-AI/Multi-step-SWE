-- R04.4: the three text forms that look like numbers to a programming
-- language and are not numbers to SQLite -- 'nan', 'inf' and a 0x hex prefix.
--
-- SQLite's text-to-number conversion has no special words and no hexadecimal
-- form: 'nan' and 'inf' are non-numeric text and convert to zero, not to a
-- floating-point special, and '0x10' converts to 0 rather than to 16, because
-- the conversion stops at the first character that cannot begin a decimal
-- number. Every row below is what stock sqlite3 3.50.2 answers.
SELECT CAST('nan' AS REAL), CAST('NaN' AS REAL), CAST('inf' AS REAL), CAST('-inf' AS REAL);
SELECT CAST('infinity' AS REAL), CAST('Inf' AS REAL), CAST('nan' AS INTEGER), CAST('inf' AS INTEGER);
SELECT typeof(CAST('nan' AS REAL)), typeof(CAST('inf' AS INTEGER));
SELECT CAST('0x10' AS INTEGER), CAST('0x10' AS REAL), CAST('0xff' AS NUMERIC), typeof(CAST('0x10' AS INTEGER));
