SELECT gcd(12::int, 18), gcd(0::int, 0), gcd(-12::int, 18);
SELECT lcm(4::int, 6), lcm(0::int, 5), lcm(-4::int, 6);
SELECT gcd(12::int8, 18::int8), lcm(4::int8, 6::int8);
SELECT gcd(1.5::numeric, 2), lcm(1.5::numeric, 2);
