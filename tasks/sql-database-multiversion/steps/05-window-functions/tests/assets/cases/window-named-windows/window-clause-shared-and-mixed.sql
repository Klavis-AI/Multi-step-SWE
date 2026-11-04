-- PROFILE.md 5.6: one name used by several window function invocations in the
-- same SELECT, including invocations of different functions; a named window
-- and an inline OVER ( ... ) in the same SELECT; and a WINDOW clause defining
-- several names separated by commas.
CREATE TABLE t(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO t VALUES(1,'x',10),(2,'x',20),(3,'y',30),(4,'y',40);
-- one name, four invocations of four different functions, plus one inline
-- window in the same result set
SELECT k, sum(v) OVER w AS s,
          count(*) OVER w AS c,
          max(v) OVER w AS m,
          last_value(v) OVER w AS lv,
          min(v) OVER (ORDER BY k) AS inline_min
FROM t WINDOW w AS (PARTITION BY g ORDER BY k) ORDER BY k;
-- two names, and a definition that is referred to by exactly one invocation
SELECT k, row_number() OVER wa AS ra, rank() OVER wb AS rb
FROM t WINDOW wa AS (ORDER BY k), wb AS (PARTITION BY g ORDER BY v) ORDER BY k;
-- a named window whose definition is empty: the whole result set, one frame
SELECT k, count(*) OVER wall AS n, sum(v) OVER wall AS total
FROM t WINDOW wall AS () ORDER BY k;
