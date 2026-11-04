-- windowfunctions.html, The FILTER Clause: "It is a syntax error to specify a
-- FILTER clause as part of a built-in window function invocation."  A FILTER on
-- an aggregate used as a window function is the legal case.  (PROFILE.md 5.11)
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40);
-- legal: FILTER on an aggregate window function
SELECT k, count(*) FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, sum(v) FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
-- legal: the same built-ins with no FILTER at all
SELECT k, row_number() OVER (ORDER BY k), rank() OVER (ORDER BY k) FROM t ORDER BY k;
-- a syntax error: FILTER on a built-in window function
SELECT k, row_number() FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, rank() FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, lag(v) FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, ntile(2) FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
SELECT k, first_value(v) FILTER (WHERE v > 15) OVER (ORDER BY k) FROM t ORDER BY k;
-- nothing above changed the table
SELECT k, v FROM t ORDER BY k;
PRAGMA integrity_check;
