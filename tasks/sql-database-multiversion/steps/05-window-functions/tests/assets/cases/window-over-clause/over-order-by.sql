-- windowfunctions.html, Introduction to Window Functions: "Window functions
-- are distinguished from ordinary SQL functions by the presence of an OVER
-- clause."  The table and the first query are the page's own.  (PROFILE.md 5.1)
CREATE TABLE t0(x INTEGER PRIMARY KEY, y TEXT);
INSERT INTO t0 VALUES (1, 'aaa'), (2, 'ccc'), (3, 'bbb');
SELECT x, y, row_number() OVER (ORDER BY y) AS row_number FROM t0 ORDER BY x;
-- "Note that this does not affect the order in which results are returned from
-- the overall query.  The order of the final output is still governed by the
-- ORDER BY clause attached to the SELECT statement."  Same window, three
-- different output orders, same three window values.  (PROFILE.md 5.2)
SELECT x, y, row_number() OVER (ORDER BY y) AS rn FROM t0 ORDER BY y;
SELECT x, y, row_number() OVER (ORDER BY y) AS rn FROM t0 ORDER BY x DESC;
-- the window ORDER BY takes the ordering-term forms of step 1 section 1
SELECT x, y, row_number() OVER (ORDER BY y DESC) AS rn FROM t0 ORDER BY x;
SELECT x, y, row_number() OVER (ORDER BY x) AS rn FROM t0 ORDER BY x;
-- PROFILE.md 5.1: "An ordering-term inside a window-defn is the ordering-term
-- step 1 section 1 already puts in scope for ORDER BY, so ASC, DESC and
-- COLLATE may be written there."
CREATE TABLE m(x INTEGER PRIMARY KEY, y TEXT);
INSERT INTO m VALUES(1,'b'),(2,'A'),(3,'C');
SELECT x, y, row_number() OVER (ORDER BY y) AS bin,
             row_number() OVER (ORDER BY y COLLATE NOCASE) AS nocase,
             row_number() OVER (ORDER BY y ASC) AS asc_explicit
  FROM m ORDER BY x;
