-- R01.15: scalar expressions evaluated with no table involved.
--
-- R01.15 asks that expressions with no table behind them answer exactly what
-- stock sqlite3 answers, and this case is where that is observed: integer and
-- real arithmetic, integer division and modulo with both signs of operand --
-- which is where an implementation that floors instead of truncating, or that
-- takes the sign of the divisor, diverges -- the comparison operators over
-- integers, reals and text, a comparison against NULL, `typeof()` over all
-- five storage classes, and `length()` and `abs()`. Being a `.sql` case it is
-- replayed against stock sqlite3 automatically, so every line above is checked
-- against the second implementation rather than frozen from this engine alone.
--
-- Each multi-value row carries a short leading literal so that the row cannot
-- collapse to a single column, which keeps the blank-line normalisation the
-- grading path applies from ever being able to swallow a real result row.
SELECT 1;
SELECT 'abc';
SELECT 1+2, 3*4, 10/4, 10%3;
SELECT 1 < 2, 2 < 1, 'a' = 'a';
SELECT 'd', 7/2, -7/2, 7%2, -7%2;
SELECT 'r', 7.0/2, 1.5+1.5;
SELECT 't', typeof(1), typeof(1.5), typeof('x'), typeof(x'00'), typeof(NULL);
SELECT 'c', 1='1', 1<2.0, 'a'<'b', NULL<1;
SELECT 'cmp', 3>=2, 2>=2, 1>=2, 3<>2, 2<>2;
SELECT 'len', length('hello');
SELECT 'abs', abs(-7);
