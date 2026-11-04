-- windowfunctions.html, Built-in Window Functions: row_number() is "The number
-- of the row within the current partition ... numbered starting from 1 in the
-- order defined by the ORDER BY clause"; rank() is "The row_number() of the
-- first peer in each group - the rank of the current row with gaps";
-- dense_rank() is "the rank of the current row without gaps".
-- (PROFILE.md 5.4)
CREATE TABLE s(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO s VALUES(1,'a',10),(2,'a',10),(3,'a',20),(4,'a',20),(5,'a',30),
                    (6,'b',7),(7,'b',9);
-- rank() and dense_rank() are the same for every row of a peer group, so a
-- window ORDER BY with ties is enough to fix them (PROFILE.md 5.8)
SELECT k, v, rank() OVER (ORDER BY v), dense_rank() OVER (ORDER BY v)
  FROM s ORDER BY k;
-- row_number() depends on the position within a peer group, so its window
-- ORDER BY totally orders the partition
SELECT k, v, row_number() OVER (ORDER BY v, k) FROM s ORDER BY k;
-- "Built-in window functions honor any PARTITION BY clause ... each selected
-- row is assigned to a partition and each partition is processed separately."
SELECT k, g, v, rank() OVER (PARTITION BY g ORDER BY v),
                dense_rank() OVER (PARTITION BY g ORDER BY v),
                row_number() OVER (PARTITION BY g ORDER BY v, k)
  FROM s ORDER BY k;
