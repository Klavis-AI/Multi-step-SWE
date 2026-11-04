--TEST--
Test sprintf() function : usage variations - hexa formats with boolean values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $formatX [, mixed $arg1 [, mixed ...]])
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : hexa formats with boolean values ***\n";

// array of boolean values
$boolean_values = array(
  true,
  false,
  TRUE,
  FALSE,
);

// array of hexa formats
$hexa_formatsX = array(
  "%x", "%xx", "%lx",
  "%Lx", " %x", "%x ",
  "\t%x", "\n%x", "%4x",
  "%30x", "%[0-9A-Fa-f]", "%*x"
);

$count = 1;
foreach($boolean_values as $boolean_valueX) {
  echo "\n-- Iteration $count --\n";

  foreach($hexa_formatsX as $formatX) {
    var_dump( sprintf($formatX, $boolean_valueX) );
  }
  $count++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : hexa formats with boolean values ***

-- Iteration 1 --
string(1) "1"
string(2) "1x"
string(1) "1"
string(1) "x"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(10) "0-9A-Fa-f]"
string(1) "x"

-- Iteration 2 --
string(1) "0"
string(2) "0x"
string(1) "0"
string(1) "x"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(10) "0-9A-Fa-f]"
string(1) "x"

-- Iteration 3 --
string(1) "1"
string(2) "1x"
string(1) "1"
string(1) "x"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(10) "0-9A-Fa-f]"
string(1) "x"

-- Iteration 4 --
string(1) "0"
string(2) "0x"
string(1) "0"
string(1) "x"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(10) "0-9A-Fa-f]"
string(1) "x"
Done
