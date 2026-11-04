CREATE TABLE q2_pci(a int) PARTITION BY RANGE (a);
CREATE TABLE q2_pci_1 PARTITION OF q2_pci FOR VALUES FROM (1) TO (10);
CREATE TABLE q2_pci_2 PARTITION OF q2_pci FOR VALUES FROM (10) TO (20);
SELECT c.relname, i.inhseqno FROM pg_inherits i JOIN pg_class c ON c.oid = i.inhrelid
  WHERE i.inhparent = 'q2_pci'::regclass ORDER BY c.relname;
SELECT count(*) FROM pg_inherits WHERE inhrelid = 'q2_pci_1'::regclass;
ALTER TABLE q2_pci DETACH PARTITION q2_pci_2;
SELECT count(*) FROM pg_inherits WHERE inhparent = 'q2_pci'::regclass;
DROP TABLE q2_pci, q2_pci_2;
