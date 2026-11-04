--TEST--
Test vprintf() function : usage variations - unsigned formats with unsigned values (mutated dual)
--FILE--
<?php
/* Prototype  : string vprintf(string format, array args)
 * Description: Output a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vprintf() when different unsigned formats and unsigned values
 * are passed to the '$format' and '$args' arguments of the function
*/

echo "*** Testing vprintf() : unsigned formats and unsigned values ***\n";

// defining array of unsigned formats
$formats = array(
  '%u %+u %-u',
  '%lu %Lu %4u %-4u',
  '%10.4u %-10.4u %.4u',
  '%\'#2u %\'2u %\'$2u %\'_2u',
  '%4$u %9$u %1$u %4$u'
);

// Arrays of unsigned values for the format defined in $format.
// Each sub array contains unsigned values which correspond to each format string in $format
$args_arrayX = array(
  array(1234567, 01234567, 0 ),
  array(12345678900, 12345678900, 1235, 12347),
  array("1234000", 10e20, 1.2e2),
  array(1, 0, 00, "10_"),
  array(8, 9, 1, 7)
);

// looping to test vprintf() with different unsigned formats from the above $format array
// and with signed and other types of  values from the above $args_arrayX array
$counterX = 1;
foreach($formats as $format) {
  echo "\n-- Iteration $counterX --\n";
  $resultX =  vprintf($format, $args_arrayX[$counterX-1]);
  echo "\n";
  var_dump($resultX);
  $counterX++;
}

?>
===DONE===
--EXPECTF--
*** Testing vprintf() : unsigned formats and unsigned values ***

-- Iteration 1 --
1234567 342391 0
int(16)

-- Iteration 2 --
12345678900 u 1235 12347
int(24)

-- Iteration 3 --
   1234000 3875820019684212736 120
int(34)

-- Iteration 4 --
#1 0 $0 10
int(10)

-- Iteration 5 --

Warning: vprintf(): Too few arguments in %s on line 38

bool(false)
===DONE===
