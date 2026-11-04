--TEST--
Test sprintf() function : usage variations - int formats with float values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $format [, mixed $arg1 [, mixed ...]])
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : integer formats with float values ***\n";

// array of float type values

$float_valuesX = array (
  2147483648,
  0x800000001, // float value, beyond max positive int
  020000000001, // float value, beyond max positive int
  0.0,
  -0.1,
  1.0,
  1e5,
  -1e6,
  1E8,
  -1E9,
  10.0000000000000000005,
  10.5e+10
);

// various integer formats
$int_formatsX = array(
  "%d", "%hd", "%ld",
  "%Ld", " %d", "%d ",
  "\t%d", "\n%d", "%4d",
  "%30d", "%[0-10]", "%*d"
);

$count = 1;
foreach($float_valuesX as $float_valueX) {
  echo "\n-- Iteration $count --\n";

  foreach($int_formatsX as $format) {
    var_dump( sprintf($format, $float_valueX) );
  }
  $count++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : integer formats with float values ***

-- Iteration 1 --
string(10) "2147483648"
string(1) "d"
string(10) "2147483648"
string(1) "d"
string(11) " 2147483648"
string(11) "2147483648 "
string(11) "	2147483648"
string(11) "
2147483648"
string(10) "2147483648"
string(30) "                    2147483648"
string(5) "0-10]"
string(1) "d"

-- Iteration 2 --
string(11) "34359738369"
string(1) "d"
string(11) "34359738369"
string(1) "d"
string(12) " 34359738369"
string(12) "34359738369 "
string(12) "	34359738369"
string(12) "
34359738369"
string(11) "34359738369"
string(30) "                   34359738369"
string(5) "0-10]"
string(1) "d"

-- Iteration 3 --
string(10) "2147483649"
string(1) "d"
string(10) "2147483649"
string(1) "d"
string(11) " 2147483649"
string(11) "2147483649 "
string(11) "	2147483649"
string(11) "
2147483649"
string(10) "2147483649"
string(30) "                    2147483649"
string(5) "0-10]"
string(1) "d"

-- Iteration 4 --
string(1) "0"
string(1) "d"
string(1) "0"
string(1) "d"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-10]"
string(1) "d"

-- Iteration 5 --
string(1) "0"
string(1) "d"
string(1) "0"
string(1) "d"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-10]"
string(1) "d"

-- Iteration 6 --
string(1) "1"
string(1) "d"
string(1) "1"
string(1) "d"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(5) "0-10]"
string(1) "d"

-- Iteration 7 --
string(6) "100000"
string(1) "d"
string(6) "100000"
string(1) "d"
string(7) " 100000"
string(7) "100000 "
string(7) "	100000"
string(7) "
100000"
string(6) "100000"
string(30) "                        100000"
string(5) "0-10]"
string(1) "d"

-- Iteration 8 --
string(8) "-1000000"
string(1) "d"
string(8) "-1000000"
string(1) "d"
string(9) " -1000000"
string(9) "-1000000 "
string(9) "	-1000000"
string(9) "
-1000000"
string(8) "-1000000"
string(30) "                      -1000000"
string(5) "0-10]"
string(1) "d"

-- Iteration 9 --
string(9) "100000000"
string(1) "d"
string(9) "100000000"
string(1) "d"
string(10) " 100000000"
string(10) "100000000 "
string(10) "	100000000"
string(10) "
100000000"
string(9) "100000000"
string(30) "                     100000000"
string(5) "0-10]"
string(1) "d"

-- Iteration 10 --
string(11) "-1000000000"
string(1) "d"
string(11) "-1000000000"
string(1) "d"
string(12) " -1000000000"
string(12) "-1000000000 "
string(12) "	-1000000000"
string(12) "
-1000000000"
string(11) "-1000000000"
string(30) "                   -1000000000"
string(5) "0-10]"
string(1) "d"

-- Iteration 11 --
string(2) "10"
string(1) "d"
string(2) "10"
string(1) "d"
string(3) " 10"
string(3) "10 "
string(3) "	10"
string(3) "
10"
string(4) "  10"
string(30) "                            10"
string(5) "0-10]"
string(1) "d"

-- Iteration 12 --
string(12) "105000000000"
string(1) "d"
string(12) "105000000000"
string(1) "d"
string(13) " 105000000000"
string(13) "105000000000 "
string(13) "	105000000000"
string(13) "
105000000000"
string(12) "105000000000"
string(30) "                  105000000000"
string(5) "0-10]"
string(1) "d"
Done
