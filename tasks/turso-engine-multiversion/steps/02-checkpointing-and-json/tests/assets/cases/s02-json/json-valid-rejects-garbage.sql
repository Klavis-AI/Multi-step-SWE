-- R02.8: json_valid answers 0 for text that is not JSON rather than raising:
-- it is a
-- predicate, not a parser entry point. Anything that does raise here would
-- stop the script and change every line after it.
--
-- The predicate use below runs over an ordinary one-row table rather than over
-- a `FROM (SELECT 1)` derived table. A FROM-clause subquery is a
-- query-planner capability that no step-01 or step-02 requirement names and
-- that this package does not schedule until step 09 (R09.2); grading it here
-- would take a row from an engine that is complete against its own contract.
-- CREATE TABLE, INSERT and `SELECT count(*) FROM t WHERE ...` are all step-01
-- surface, so what this line still measures is only json_valid in a WHERE
-- clause, which is what it was written for.
SELECT json_valid('{"a":1}'), json_valid('[1,2]'), json_valid('null'), json_valid('1');
SELECT json_valid('not json'), json_valid(''), json_valid('  '), json_valid('{"a":');
CREATE TABLE j(id INTEGER PRIMARY KEY);
INSERT INTO j VALUES(1);
SELECT count(*) FROM j WHERE json_valid('not json') = 0;
SELECT json_valid('[1,2]') + json_valid('nope');
