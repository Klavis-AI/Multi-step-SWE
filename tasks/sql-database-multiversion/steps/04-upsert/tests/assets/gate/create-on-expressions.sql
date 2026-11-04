-- expridx.html: "Normally, an SQL index references columns of a table.  But
-- an index can also be formed on expressions involving table columns."  The
-- table and index below are that page's own worked example, with its
-- REFERENCES clauses omitted because foreign keys are out of scope at every
-- step (PROFILE.md 8).
CREATE TABLE account_change(
  chng_id INTEGER PRIMARY KEY,
  acct_no INTEGER,
  location INTEGER,
  amt INTEGER,          -- in cents
  authority TEXT,
  comment TEXT
);
CREATE INDEX acctchng_magnitude ON account_change(acct_no, abs(amt));
INSERT INTO account_change VALUES
  (1,7,1, 25000,'a','x'),
  (2,7,1,-10500,'b','y'),
  (3,7,2,  5000,'c','z'),
  (4,8,1,-30000,'d','w');
-- expridx.html: "to list all changes to account number $xyz that are more
-- than $100.00, one can say: SELECT * FROM account_change WHERE acct_no=$xyz
-- AND abs(amt)>=10000;"
SELECT chng_id, amt FROM account_change
  WHERE acct_no=7 AND abs(amt)>=10000 ORDER BY chng_id;
-- expridx.html: "to list all changes to one particular account ($xyz) in
-- order of decreasing magnitude, one can write: SELECT * FROM account_change
-- WHERE acct_no=$xyz ORDER BY abs(amt) DESC;"
SELECT chng_id, amt FROM account_change WHERE acct_no=7 ORDER BY abs(amt) DESC;
-- expridx.html's second worked example: "CREATE TABLE t2(x,y,z); CREATE INDEX
-- t2xy ON t2(x+y);" and the query whose WHERE term matches it exactly.
CREATE TABLE t2(x,y,z);
INSERT INTO t2 VALUES(1,2,3),(4,5,6),(-7,8,9);
CREATE INDEX t2xy ON t2(x+y);
SELECT * FROM t2 WHERE x+y=3;
SELECT x, y FROM t2 ORDER BY x+y DESC;
