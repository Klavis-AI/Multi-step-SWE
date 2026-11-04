--TEST--
Test sprintf() function : usage variations - octal formats with boolean values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $format [, mixed $arg1 [, mixed ...]])
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : octal formats with boolean values ***\n";

// array of boolean values
$boolean_valuesX = array(
  true,
  false,
  TRUE,
  FALSE,
);

// array of octal formats
$octal_formatsX = array(
  "%o", "%ho", "%lo",
  "%Lo", " %o", "%o ",
  "\t%o", "\n%o", "%4o",
  "%30o", "%[0-12]", "%*o"
);

$count = 1;
foreach($boolean_valuesX as $boolean_valueX) {
  echo "\n-- Iteration $count --\n";

  foreach($octal_formatsX as $format) {
    var_dump( sprintf($format, $boolean_valueX) );
  }
  $count++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : octal formats with boolean values ***

-- Iteration 1 --
string(1) "1"
string(1) "o"
string(1) "1"
string(1) "o"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(5) "0-12]"
string(1) "o"

-- Iteration 2 --
string(1) "0"
string(1) "o"
string(1) "0"
string(1) "o"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-12]"
string(1) "o"

-- Iteration 3 --
string(1) "1"
string(1) "o"
string(1) "1"
string(1) "o"
string(2) " 1"
string(2) "1 "
string(2) "	1"
string(2) "
1"
string(4) "   1"
string(30) "                             1"
string(5) "0-12]"
string(1) "o"

-- Iteration 4 --
string(1) "0"
string(1) "o"
string(1) "0"
string(1) "o"
string(2) " 0"
string(2) "0 "
string(2) "	0"
string(2) "
0"
string(4) "   0"
string(30) "                             0"
string(5) "0-12]"
string(1) "o"
Done
