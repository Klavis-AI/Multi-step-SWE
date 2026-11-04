-- windowfunctions.html, Built-in Window Functions: the page's own worked
-- example of lead(), lag(), first_value(), last_value() and nth_value(), over
-- its own t1.  "The frame-spec is ignored by both lag() and lead(), but
-- respected by first_value(), last_value() and nth_value()."  The only change
-- is the total ORDER BY that TEST_INTERFACE.md section 5 requires on the outer
-- SELECT; column b holds seven distinct values, so the window ORDER BY totally
-- orders the partition.  (PROFILE.md 5.4)
CREATE TABLE t1(a INTEGER PRIMARY KEY, b, c);
INSERT INTO t1 VALUES (1, 'A', 'one' ),
                      (2, 'B', 'two' ),
                      (3, 'C', 'three'),
                      (4, 'D', 'one' ),
                      (5, 'E', 'two' ),
                      (6, 'F', 'three'),
                      (7, 'G', 'one' );
SELECT b AS b,
       lead(b, 2, 'n/a') OVER win AS lead,
       lag(b) OVER win AS lag,
       first_value(b) OVER win AS first_value,
       last_value(b) OVER win AS last_value,
       nth_value(b, 3) OVER win AS nth_value_3
FROM t1
WINDOW win AS (ORDER BY b ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
ORDER BY b;
