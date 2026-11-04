SELECT gcd((-2147483648)::int4, 0);
SELECT lcm(2147483647::int4, 2147483646::int4);
SELECT lcm(0::int, 0);
SELECT div(1::numeric, 0);
SELECT gcd('NaN'::numeric, 1);
