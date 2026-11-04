-- datatype3.html section 6.1: "The expression "x IN (SELECT y ...)" is handled
-- in the same way as the expression "x = y" for the purposes of determining
-- the collating sequence." and "The collating sequence used for expressions of
-- the form "x IN (y, z, ...)" is the collating sequence of x."  Section 3.3
-- says the same of affinity: "Datatype conversions in comparisons of the form
-- "x IN (SELECT y ...)" are handled is if the comparison were really "x=y"."
-- PROFILE.md section 2 makes datatype3.html normative and fully in scope.
CREATE TABLE t(a TEXT COLLATE NOCASE);
CREATE TABLE u(x TEXT);
INSERT INTO t VALUES('ABC');
INSERT INTO u VALUES('abc');
-- the value-list form uses the collating sequence of the left operand
SELECT a IN ('abc'), a NOT IN ('abc') FROM t;
-- the subquery form is handled as "a = x", so the same NOCASE applies
SELECT count(*) FROM t WHERE a IN (SELECT 'abc');
SELECT count(*) FROM t WHERE a IN (SELECT x FROM u);
-- with the BINARY column on the left the comparison is BINARY
SELECT count(*) FROM u WHERE x IN (SELECT a FROM t);
-- affinity in the subquery form is handled as "x = y" too
CREATE TABLE n(b NUMERIC);
INSERT INTO n VALUES('500');
SELECT b IN (SELECT '500'), b IN (SELECT 500) FROM n;
