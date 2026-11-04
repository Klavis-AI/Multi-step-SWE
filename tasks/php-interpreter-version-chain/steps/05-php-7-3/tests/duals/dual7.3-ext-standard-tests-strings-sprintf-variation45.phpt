--TEST--
Test sprintf() function : usage variations - unsigned formats with boolean values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $formatX [, mixed $arg1 [, mixed ...]])
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : unsigned formats with boolean values ***\n";

// array of boolean values
$boolean_values = array(
  true,
  false,
  TRUE,
  FALSE,
);

// array of unsigned formats
$unsigned_formatsX = array(
  "%u", "%hu", "%lu",
  "%Lu", " %u", "%u ",
  "\t%u", "\n%u", "%4u",
   "%30u", "%[0-11]", "%*u"
);

$countX = 1;
foreach($boolean_values as $boolean_value) {
  echo "\n-- Iteration $countX --\n";

  foreach($unsigned_formatsX as $formatX) {
    var_dump( sprintf($formatX, $boolean_value) );
  }
  $countX++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : unsigned formats with boolean values ***

-- Iteration 1 --
string(1) "1"
string(1) "u"
string(1) "1"
string(1) "u"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(5) "0-11]"
string(1) "u"

-- Iteration 2 --
string(1) "0"
string(1) "u"
string(1) "0"
string(1) "u"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-11]"
string(1) "u"

-- Iteration 3 --
string(1) "1"
string(1) "u"
string(1) "1"
string(1) "u"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(5) "0-11]"
string(1) "u"

-- Iteration 4 --
string(1) "0"
string(1) "u"
string(1) "0"
string(1) "u"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-11]"
string(1) "u"
Done
