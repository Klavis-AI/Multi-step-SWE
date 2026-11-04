-- windowfunctions.html: ntile(N) "divides the partition into N groups as
-- evenly as possible and assigns an integer between 1 and N to each group, in
-- the order defined by the ORDER BY clause ... If necessary, larger groups
-- occur first."  The window ORDER BY totally orders the partition, because an
-- ntile assignment inside a peer group is not fixed (PROFILE.md 5.4, 5.8).
CREATE TABLE t(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO t VALUES(1,10),(2,20),(3,30),(4,40),(5,50),(6,60);
-- six rows: two groups of three, three groups of two, and -- the page's own
-- ntile(4) case -- "two groups of two and two groups of one.  The larger
-- groups of two appear first."
SELECT k, ntile(1) OVER (ORDER BY v),
          ntile(2) OVER (ORDER BY v),
          ntile(3) OVER (ORDER BY v),
          ntile(4) OVER (ORDER BY v),
          ntile(6) OVER (ORDER BY v)
  FROM t ORDER BY k;
-- five rows into two groups: three then two, larger first
CREATE TABLE f(k INTEGER PRIMARY KEY, v INTEGER);
INSERT INTO f VALUES(1,1),(2,2),(3,3),(4,4),(5,5);
SELECT k, ntile(2) OVER (ORDER BY v), ntile(3) OVER (ORDER BY v) FROM f ORDER BY k;
-- ntile honours PARTITION BY like every other built-in
CREATE TABLE p(k INTEGER PRIMARY KEY, g TEXT, v INTEGER);
INSERT INTO p VALUES(1,'x',1),(2,'x',2),(3,'x',3),(4,'x',4),(5,'y',5),(6,'y',6);
SELECT k, g, ntile(2) OVER (PARTITION BY g ORDER BY v) FROM p ORDER BY k;
