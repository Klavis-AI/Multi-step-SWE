-- R09.5: INSERT INTO ... SELECT stores exactly the rows the SELECT produced,
-- including a projection that computes its values, and changes() afterwards
-- reports how many rows that statement inserted -- zero when it selected
-- none.
CREATE TABLE src(a INTEGER, b TEXT);
CREATE TABLE dst(a INTEGER, b TEXT);
INSERT INTO src VALUES(1,'x'),(2,'y'),(3,'z'),(4,'w');
INSERT INTO dst SELECT a, b FROM src WHERE a <= 3;
SELECT 'changes:'||changes();
SELECT 'count:'||count(*) FROM dst;
SELECT 'row:'||a||'/'||b FROM dst ORDER BY a;
INSERT INTO dst SELECT a, b FROM src WHERE a = 4;
SELECT 'changes2:'||changes();
SELECT 'count2:'||count(*) FROM dst;
INSERT INTO dst SELECT a, b FROM src WHERE a > 100;
SELECT 'changes3:'||changes();
SELECT 'count3:'||count(*) FROM dst;
INSERT INTO dst SELECT a*10, upper(b) FROM src WHERE a=1;
SELECT 'changes4:'||changes();
SELECT 'mapped:'||a||'/'||b FROM dst WHERE a=10;
SELECT 'final:'||count(*) FROM dst;
