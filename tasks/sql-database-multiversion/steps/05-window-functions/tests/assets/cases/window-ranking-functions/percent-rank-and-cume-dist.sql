-- windowfunctions.html: percent_rank() "always returns a value between 0.0 and
-- 1.0 equal to ( rank - 1)/( partition-rows - 1)"; "If the partition contains
-- only one row, this function returns 0.0".  cume_dist() is "row-number /
-- partition-rows, where row-number is the value returned by row_number() for
-- the last peer in the group".  Both return REAL, so PROFILE.md 5.4 uses
-- partition sizes whose results print exactly.
CREATE TABLE r(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO r VALUES(1,10),(2,20),(3,30),(4,40),(5,50);
-- five rows, no ties: percent_rank is (rank-1)/4 and cume_dist is row/5
SELECT k, percent_rank() OVER (ORDER BY v), cume_dist() OVER (ORDER BY v)
  FROM r ORDER BY k;
-- with a peer group at the head: both functions are the same for both peers
CREATE TABLE q(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO q VALUES(1,10),(2,10),(3,30),(4,40),(5,50);
SELECT k, rank() OVER (ORDER BY v), percent_rank() OVER (ORDER BY v),
          cume_dist() OVER (ORDER BY v)
  FROM q ORDER BY k;
-- the storage class of each result (PROFILE.md 5.4, TEST_INTERFACE.md 2.3)
SELECT DISTINCT typeof(percent_rank() OVER (ORDER BY v)),
                typeof(cume_dist() OVER (ORDER BY v)),
                typeof(rank() OVER (ORDER BY v)),
                typeof(row_number() OVER (ORDER BY v, k))
  FROM q;
-- a partition of one row
CREATE TABLE one(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO one VALUES(1,5);
SELECT percent_rank() OVER (ORDER BY v), cume_dist() OVER (ORDER BY v) FROM one;
-- partitions of five and of one in the same query
CREATE TABLE m(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO m VALUES(1,'x',1),(2,'x',2),(3,'x',3),(4,'x',4),(5,'x',5),(6,'y',9);
SELECT k, g, percent_rank() OVER (PARTITION BY g ORDER BY v),
             cume_dist() OVER (PARTITION BY g ORDER BY v)
  FROM m ORDER BY k;
