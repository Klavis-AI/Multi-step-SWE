--TEST--
Test sprintf() function : usage variations - scientific formats with string values (mutated dual)
--FILE--
<?php
/* Prototype  : string sprintf(string $format [, mixed $arg1X [, mixed ...]])
 * Description: Return a formatted string 
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing sprintf() : scientific formats with string values ***\n";

// array of string values 
$string_values = array(
  "",
  '',
  "0",
  '0',
  "1",
  '1',
  "\x01",
  '\x01',
  "\01",
  '\01',
  'gnirts',
  "string",
  "true",
  "FALSE",
  'false',
  'TRUE',
  "NULL",
  'null',
  "333333333333333333333333333333"
);

// array of scientific formats
$scientific_formatsX = array(
  "%e", "%he", "%le",
  "%Le", " %e", "%e ",
  "\t%e", "\n%e", "%4e",
  "%30e", "%[0-1]", "%*e"
);


$countX = 1;
foreach($string_values as $string_value) {
  echo "\n-- Iteration $countX --\n";
  
  foreach($scientific_formatsX as $format) {
    var_dump( sprintf($format, $string_value) );
  }
  $countX++;
};

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : scientific formats with string values ***

-- Iteration 1 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 2 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 3 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 4 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 5 --
string(11) "1.000000e+0"
string(1) "e"
string(11) "1.000000e+0"
string(1) "e"
string(12) " 1.000000e+0"
string(12) "1.000000e+0 "
string(12) "	1.000000e+0"
string(12) "
1.000000e+0"
string(11) "1.000000e+0"
string(30) "                   1.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 6 --
string(11) "1.000000e+0"
string(1) "e"
string(11) "1.000000e+0"
string(1) "e"
string(12) " 1.000000e+0"
string(12) "1.000000e+0 "
string(12) "	1.000000e+0"
string(12) "
1.000000e+0"
string(11) "1.000000e+0"
string(30) "                   1.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 7 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 8 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 9 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 10 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 11 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 12 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 13 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 14 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 15 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 16 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 17 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 18 --
string(11) "0.000000e+0"
string(1) "e"
string(11) "0.000000e+0"
string(1) "e"
string(12) " 0.000000e+0"
string(12) "0.000000e+0 "
string(12) "	0.000000e+0"
string(12) "
0.000000e+0"
string(11) "0.000000e+0"
string(30) "                   0.000000e+0"
string(4) "0-1]"
string(1) "e"

-- Iteration 19 --
string(12) "3.333333e+29"
string(1) "e"
string(12) "3.333333e+29"
string(1) "e"
string(13) " 3.333333e+29"
string(13) "3.333333e+29 "
string(13) "	3.333333e+29"
string(13) "
3.333333e+29"
string(12) "3.333333e+29"
string(30) "                  3.333333e+29"
string(4) "0-1]"
string(1) "e"
Done
