CREATE TABLE q3_cvc(a int, b int, c text);
ALTER TABLE q3_cvc ADD CONSTRAINT q3_cvc_pk PRIMARY KEY (a) INCLUDE (b);
ALTER TABLE q3_cvc ADD CONSTRAINT q3_cvc_uq UNIQUE (b) INCLUDE (c);
SELECT conname, contype FROM pg_constraint WHERE conrelid = 'q3_cvc'::regclass ORDER BY conname;
SELECT indexname, indexdef FROM pg_indexes WHERE tablename = 'q3_cvc' ORDER BY indexname;
INSERT INTO q3_cvc VALUES (1,1,'x');
INSERT INTO q3_cvc VALUES (1,2,'y');
DROP TABLE q3_cvc;
