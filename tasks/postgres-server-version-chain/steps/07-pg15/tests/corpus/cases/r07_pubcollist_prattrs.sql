CREATE TABLE x07_27a(a int PRIMARY KEY, b text, c int);
CREATE PUBLICATION x07_27p FOR TABLE x07_27a (a, c);
SELECT r.prattrs FROM pg_publication_rel r
  JOIN pg_publication p ON p.oid = r.prpubid WHERE p.pubname='x07_27p';
SELECT r.prattrs IS NULL AS null_attrs FROM pg_publication_rel r
  JOIN pg_publication p ON p.oid = r.prpubid WHERE p.pubname='x07_27p';
DROP PUBLICATION x07_27p;
CREATE PUBLICATION x07_27q FOR TABLE x07_27a;
SELECT r.prattrs IS NULL AS null_attrs FROM pg_publication_rel r
  JOIN pg_publication p ON p.oid = r.prpubid WHERE p.pubname='x07_27q';
DROP PUBLICATION x07_27q;
DROP TABLE x07_27a;
