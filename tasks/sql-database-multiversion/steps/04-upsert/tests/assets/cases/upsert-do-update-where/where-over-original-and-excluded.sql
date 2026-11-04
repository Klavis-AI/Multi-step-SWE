-- The trailing WHERE clause may read the original row and the excluded row,
-- and lang_insert.html says the AS alias "can be used within WHERE and SET
-- clauses of the UPSERT" (PROFILE.md 4.1, 4.4).
CREATE TABLE stock(sku TEXT PRIMARY KEY, qty INTEGER, tag TEXT);
INSERT INTO stock VALUES('a',5,'old'),('b',0,'old'),('c',7,'old');
-- the alias stands for the original row on both sides of the clause
INSERT INTO stock AS s VALUES('a',3,'new'),('b',4,'new'),('d',1,'new')
  ON CONFLICT(sku) DO UPDATE SET qty=s.qty+excluded.qty, tag=excluded.tag
  WHERE s.qty = 0;
SELECT sku,qty,tag FROM stock ORDER BY sku;
-- a clause over excluded. alone
INSERT INTO stock AS s VALUES('a',100,'big'),('c',1,'small')
  ON CONFLICT(sku) DO UPDATE SET qty=excluded.qty, tag=excluded.tag
  WHERE excluded.qty > 10;
SELECT sku,qty,tag FROM stock ORDER BY sku;
-- an unqualified column name in the clause means the original value too
INSERT INTO stock VALUES('c',2,'lower')
  ON CONFLICT(sku) DO UPDATE SET qty=excluded.qty, tag=excluded.tag
  WHERE qty > excluded.qty;
SELECT sku,qty,tag FROM stock ORDER BY sku;
PRAGMA integrity_check;
