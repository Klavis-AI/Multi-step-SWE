CREATE TABLE po(id INTEGER PRIMARY KEY, parent_po);
CREATE INDEX po_parent ON po(parent_po) WHERE parent_po IS NOT NULL;
INSERT INTO po VALUES(1,NULL),(2,1),(3,1),(4,NULL);
SELECT sql FROM sqlite_master WHERE name = 'po_parent';
SELECT id FROM po WHERE parent_po = 1 ORDER BY id;
SELECT id FROM po WHERE parent_po IS NULL ORDER BY id;
