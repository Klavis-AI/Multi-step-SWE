--TEST--
Test vsprintf() function : usage variations - unsigned formats with unsigned values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different unsigned formats and unsigned values
 * are passed to the '$formatX' and '$args' arguments of the function
*/

echo "*** Testing vsprintf() : unsigned formats and unsigned values ***\n";

// defining array of unsigned formats
$formatsX = array(
  '%u %+u %-u',
  '%lu %Lu %4u %-4u',
  '%10.4u %-10.4u %.4u',
  '%\'#2u %\'2u %\'$2u %\'_2u',
  '%6$u %6$u %1$u %5$u'
);

// Arrays of unsigned values for the format defined in $formatX.
// Each sub array contains unsigned values which correspond to each format string in $formatX
$args_array = array(
  array(1234567, 01234567, 0 ),
  array(12345678900, 12345678900, 1235, 12350),
  array("1234000", 10e20, 1.2e2),
  array(1, 0, 00, "10_"),
  array(8, 5, 1, 4)
);

// looping to test vsprintf() with different unsigned formats from the above $formatX array
// and with signed and other types of  values from the above $args_array array
$counterX = 1;
foreach($formatsX as $formatX) {
  echo "\n-- Iteration $counterX --\n";
  var_dump( vsprintf($formatX, $args_array[$counterX-1]) );
  $counterX++;
}

echo "Done";
?>
--EXPECTF--
*** Testing vsprintf() : unsigned formats and unsigned values ***

-- Iteration 1 --
string(16) "1234567 342391 0"

-- Iteration 2 --
string(24) "12345678900 u 1235 12350"

-- Iteration 3 --
string(34) "   1234000 3875820019684212736 120"

-- Iteration 4 --
string(10) "#1 0 $0 10"

-- Iteration 5 --

Warning: vsprintf(): Too few arguments in %s on line 38
bool(false)
Done
