-- windowfunctions.html, Introduction: "Named window-defn clauses may also be
-- added to a SELECT statement using a WINDOW clause and then referred to by
-- name within window function invocations.  For example, the following SELECT
-- statement contains two named window-defs clauses, 'win1' and 'win2'".
-- The first query is the page's own, unchanged -- it already carries the total
-- ORDER BY x that TEST_INTERFACE.md section 5 requires.  (PROFILE.md 5.6)
CREATE TABLE t0(x INTEGER PRIMARY KEY, y TEXT);
INSERT INTO t0 VALUES (1, 'aaa'), (2, 'ccc'), (3, 'bbb');
SELECT x, y, row_number() OVER win1, rank() OVER win2
FROM t0
WINDOW win1 AS (ORDER BY y RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW),
       win2 AS (PARTITION BY y ORDER BY x)
ORDER BY x;
-- a name referred to by an invocation carries no parentheses: "OVER win1"
