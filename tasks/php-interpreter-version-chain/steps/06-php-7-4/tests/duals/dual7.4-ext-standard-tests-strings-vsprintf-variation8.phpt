--TEST--
Test vsprintf() function : usage variations - string formats with non-string values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different string formats and non-string values are passed to
 * the '$format' and '$argsX' arguments of the function
*/

error_reporting(E_ALL & ~E_NOTICE);

echo "*** Testing vsprintf() : string formats and non-string values ***\n";

// defining array of string formats
$formatsX =
  '%s %+s %-s
   %ls %4s %-4s
   %10.4s %-10.4s %04s %04.4s
   %\'#2s %\'2s %\'$2s %\'_2s
   %4$s %6$s %1$s %3$s';

// Arrays of non string values for the format defined in $format.
// Each sub array contains non string values which correspond to each format in $format
$args_array = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, -1234.6789, +1234.6789,
        2.1234567e10, +2.7654321e10, -2.7654321e10, 2.1234567e10,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

 // array of int values
 array(3, -3, +4,
       123456, -12346789, +12346789,
       123200, +20005, -40002, 22215,
       12345780, 1211111, -12111111, -12345634,
       6, +6, 1,-3 ),


  // different arrays
  array( array(0), array(1, 3), array(-1, -1),
         array("126"), array('-125'), array("-126"),
         array(true), array(false), array(TRUE), array(FALSE),
         array("123hello"), array("1", "5"), array('123hello'), array(13=>"12twelve"),
         array("4"), array("9"), array("1"), array("7") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, FALSE, 1,
         true, false, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different string formats from the above $format array
// and with non-string values from the above $args_array array
$counterX = 1;
foreach($args_array as $argsX) {
  echo "\n-- Iteration $counterX --\n";
  var_dump( vsprintf($formatsX, $argsX) );
  $counterX++;
}

?>
===DONE===
--EXPECT--
*** Testing vsprintf() : string formats and non-string values ***

-- Iteration 1 --
string(179) "2.2 0.2 10.2
   123456.234 -1234.6789 1234.6789
         2123 2765       -27654321000 2123
   12345.78 12.000000011111 -12.00000111111 -123456.234
   123456.234 1234.6789 2.2 10.2"

-- Iteration 2 --
string(135) "3 -3 4
   123456 -12346789 12346789
         1232 2000       -40002 2221
   12345780 1211111 -12111111 -12345634
   123456 12346789 3 4"

-- Iteration 3 --
string(128) "Array Array Array
   Array Array Array
         Arra Arra       Array Arra
   Array Array Array Array
   Array Array Array Array"

-- Iteration 4 --
string(78) "1 1 
   1      1   
            1            0001 0000
   #0 1 $1 _0
   1 1 1 "
===DONE===
