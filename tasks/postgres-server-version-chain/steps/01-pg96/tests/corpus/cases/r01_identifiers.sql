CREATE TABLE "Mixed Case"("A b" int, normal int);
INSERT INTO "Mixed Case" VALUES (1,2);
SELECT "A b", normal FROM "Mixed Case";
SELECT * FROM "mixed case";
SELECT a_b FROM (SELECT 1 AS "a_b") x;
DROP TABLE "Mixed Case";
