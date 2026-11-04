-- windowfunctions.html, Built-in Window Functions: the page's own worked
-- example of the ranking functions, over its own t2.  PROFILE.md 5.4 keeps two
-- of its six columns out of the comparison -- cume_dist, whose printed 0.66 is
-- a rounded 4/6, and row_number, which depends on the order inside a peer
-- group -- and compares the other four.
CREATE TABLE t2(a, b);
INSERT INTO t2 VALUES('a', 'one'),
                     ('a', 'two'),
                     ('a', 'three'),
                     ('b', 'four'),
                     ('c', 'five'),
                     ('c', 'six');
SELECT a AS a,
       rank() OVER win AS rank,
       dense_rank() OVER win AS dense_rank,
       percent_rank() OVER win AS percent_rank
FROM t2
WINDOW win AS (ORDER BY a)
ORDER BY a, b;
-- the same three functions with no ORDER BY term: "If there is no ORDER BY
-- clause, then all rows are considered peers and this function always returns
-- 1", and percent_rank is then (1-1)/(6-1)
SELECT DISTINCT rank() OVER (), dense_rank() OVER (), percent_rank() OVER ()
  FROM t2;
