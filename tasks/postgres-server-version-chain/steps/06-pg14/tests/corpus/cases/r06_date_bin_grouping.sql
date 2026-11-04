CREATE TABLE x06_53a(t timestamp, v int);
INSERT INTO x06_53a VALUES
  ('2020-02-11 15:01:00',1),('2020-02-11 15:14:00',2),
  ('2020-02-11 15:16:00',3),('2020-02-11 16:05:00',4);
SELECT date_bin('15 minutes', t, timestamp '2020-01-01') AS bin, sum(v)
  FROM x06_53a GROUP BY 1 ORDER BY 1;
SELECT date_trunc('hour', t) AS h, date_bin('1 hour', t, timestamp '2020-01-01') AS b
  FROM x06_53a ORDER BY t;
DROP TABLE x06_53a;
