--TEST--
Test sprintf() function : usage variations - unsigned formats with array values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $format [, mixed $arg1X [, mixed ...]])
 * Description: Return a formatted string 
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : unsigned formats with array values ***\n";

// array of array values 
$array_valuesX = array(
  array(),
  array(0),
  array(1),
  array(-12346),
  array(+12350),
  array(NULL),
  array(null),
  array("string"),
  array(true),
  array(TRUE),
  array(false),
  array(FALSE),
  array(1,4,4,9),
  array(1 => "One", "two" => 3)
);

// array of unsigned formats
$unsigned_formats = array( 
  "%u", "%hu", "%lu",
  "%Lu", " %u", "%u ",   
  "\t%u", "\n%u", "%4u", 
   "%30u", "%[0-11]", "%*u"
);

$count = 1;
foreach($array_valuesX as $array_valueX) {
  echo "\n-- Iteration $count --\n";
  
  foreach($unsigned_formats as $format) {
    var_dump( sprintf($format, $array_valueX) );
  }
  $count++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : unsigned formats with array values ***

-- Iteration 1 --
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

-- Iteration 2 --
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

-- Iteration 5 --
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

-- Iteration 6 --
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

-- Iteration 7 --
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

-- Iteration 8 --
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

-- Iteration 9 --
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

-- Iteration 10 --
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

-- Iteration 11 --
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

-- Iteration 12 --
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

-- Iteration 13 --
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

-- Iteration 14 --
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
Done
