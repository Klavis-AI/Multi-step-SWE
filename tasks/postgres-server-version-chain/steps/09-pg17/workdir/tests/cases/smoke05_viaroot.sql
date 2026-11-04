CREATE TABLE sv(i int) PARTITION BY RANGE (i);
CREATE TABLE sv1 PARTITION OF sv FOR VALUES FROM (0) TO (9);
CREATE PUBLICATION svp FOR TABLE sv WITH (publish_via_partition_root = true);
SELECT pubviaroot FROM pg_publication WHERE pubname='svp';
DROP PUBLICATION svp;
DROP TABLE sv;
