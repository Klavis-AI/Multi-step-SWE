CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(3,'c'),(1,'a'),(NULL,NULL),(2,'b');
SELECT min(a), max(a), min(b), max(b) FROM t;
SELECT group_concat(b) FROM t WHERE b = 'a';
SELECT length(group_concat(b)), length(group_concat(b, '--')) FROM t;
SELECT count(*) FROM t WHERE instr(( SELECT group_concat(b) FROM t ), b) > 0;
-- PROFILE.md section 4: min, max and group_concat with DISTINCT too. The
-- concatenation order is arbitrary (lang_aggfunc.html), so only its length
-- is compared, over a source that really has a duplicate to drop.
SELECT min(DISTINCT a), max(DISTINCT a) FROM t;
SELECT length(group_concat(DISTINCT b)) FROM
  (SELECT 'a' AS b UNION ALL SELECT 'a' UNION ALL SELECT 'bb');
DELETE FROM t;
SELECT min(a) IS NULL, max(a) IS NULL, group_concat(b) IS NULL FROM t;
