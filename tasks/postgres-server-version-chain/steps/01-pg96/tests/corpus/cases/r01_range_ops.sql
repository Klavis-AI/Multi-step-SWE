SELECT int4range(1,5) @> 3, int4range(1,5) @> int4range(2,3), 3 <@ int4range(1,5);
SELECT int4range(1,5) && int4range(4,9), int4range(1,5) && int4range(6,9);
SELECT int4range(1,5) << int4range(6,9), int4range(6,9) >> int4range(1,5);
SELECT int4range(1,5) -|- int4range(5,9), int4range(1,5) * int4range(3,9);
SELECT int4range(1,5) + int4range(4,9);
SELECT int4range(1,5) + int4range(7,9);
SELECT int4range(5,1);
