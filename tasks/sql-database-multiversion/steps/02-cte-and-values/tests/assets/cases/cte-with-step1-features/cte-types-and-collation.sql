-- The value model of step 1 section 2 is unchanged under a WITH clause.
CREATE TABLE t(i INTEGER, r REAL, s TEXT, b BLOB, n, c TEXT COLLATE NOCASE);
INSERT INTO t VALUES(42, 2.5, 'hi', X'4142', NULL, 'Ap');
WITH v(i, r, s, b, n) AS (SELECT i, r, s, b, n FROM t)
  SELECT typeof(i), typeof(r), typeof(s), typeof(b), typeof(n), hex(b) FROM v;
-- datatype3.html section 6.1, rule 1: an operand carrying an explicit
-- postfix COLLATE decides the comparison, whatever collating function the
-- columns on either side carry. Rule 2 decides the second comparison, whose
-- left operand is a real table column declared COLLATE NOCASE. Whether a
-- common table expression's column propagates the collating sequence of the
-- table column it is selected from is not stated anywhere in this corpus and
-- is deliberately not asserted here.
WITH v(c) AS (SELECT c FROM t) SELECT c FROM v WHERE c = 'AP' COLLATE NOCASE;
SELECT c FROM t WHERE c = 'AP';
WITH v(i, s, n, r, b) AS (VALUES(1,'a',NULL,2.5,X'41'))
  SELECT typeof(i), typeof(s), typeof(n), typeof(r), typeof(b), hex(b) FROM v;
CREATE TABLE aff(x INTEGER);
WITH s(y) AS (SELECT '42') INSERT INTO aff SELECT y FROM s;
SELECT typeof(x), x FROM aff;
