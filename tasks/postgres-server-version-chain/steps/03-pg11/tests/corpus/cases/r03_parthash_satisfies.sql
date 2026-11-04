CREATE TABLE q3_hss(a int) PARTITION BY HASH (a);
CREATE TABLE q3_hss_0 PARTITION OF q3_hss FOR VALUES WITH (MODULUS 2, REMAINDER 0);
CREATE TABLE q3_hss_1 PARTITION OF q3_hss FOR VALUES WITH (MODULUS 2, REMAINDER 1);
SELECT satisfies_hash_partition('q3_hss'::regclass, 2, 0, 1)
    OR satisfies_hash_partition('q3_hss'::regclass, 2, 1, 1) AS covered;
SELECT satisfies_hash_partition('q3_hss'::regclass, 2, 0, 1)
   AND satisfies_hash_partition('q3_hss'::regclass, 2, 1, 1) AS both;
INSERT INTO q3_hss VALUES (1);
SELECT satisfies_hash_partition('q3_hss'::regclass, 2, 0, 1) =
       (SELECT tableoid = 'q3_hss_0'::regclass FROM q3_hss WHERE a = 1) AS agrees;
DROP TABLE q3_hss;
