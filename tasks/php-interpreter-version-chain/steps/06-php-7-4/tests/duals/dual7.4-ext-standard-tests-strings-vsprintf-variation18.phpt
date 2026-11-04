--TEST--
Test vsprintf() function : usage variations - scientific formats with non-scientific values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different scientific formats and non-scientific values are passed to
 * the '$formatX' and '$argsX' arguments of the function
*/

echo "*** Testing vsprintf() : scientific formats and non-scientific values ***\n";

// defining array of non-scientific formats
$formatsX =
  '%e %+e %-e
   %le %Le %4e %-4e
   %10.4e %-10.4e %04e %04.4e
   %\'#2e %\'2e %\'$2e %\'_2e
   %8$e %7$e %1$e %4$e';

// Arrays of non scientific values for the format defined in $formatX.
// Each sub array contains non scientific values which correspond to each format in $formatX
$args_array = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, 123456.234, -1234.6789, +1234.6789,
        20.00, +212.2, -411000000000, 2212.000000000001,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

  // array of strings
  array(" ", ' ', 'olleh',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", '0123456hello', 'olle654321h',
        "1234hello", "hello\0world", "NULL", "true",
        "6", "7", '1', '4'),

  // different arrays
  array( array(0), array(1, 7), array(-1, -1),
         array("126"), array('126'), array('-128'), array("-126"),
         array(true), array(false), array(TRUE), array(FALSE),
         array("123hello"), array("1", "3"), array('123hello'), array(15=>"12twelve"),
         array("4"), array("9"), array("1"), array("5") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, false, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different scientific formats from the above $formatX array
// and with non-scientific values from the above $args_array array
$counter = 1;
foreach($args_array as $argsX) {
  echo "\n-- Iteration $counter --\n";
  var_dump( vsprintf($formatsX, $argsX) );
  $counter++;
}

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : scientific formats and non-scientific values ***

-- Iteration 1 --
string(231) "2.200000e+0 +2.000000e-1 1.020000e+1
   1.234562e+5 e -1.234679e+3 1.234679e+3
    2.0000e+1 2.1220e+2  -4.110000e+11 2.2120e+3
   1.234578e+4 1.200000e+1 -1.200000e+1 -1.234562e+5
   2.000000e+1 1.234679e+3 2.200000e+0 1.234562e+5"

-- Iteration 2 --
string(227) "0.000000e+0 +0.000000e+0 0.000000e+0
   1.230000e+2 e -1.230000e+2 1.230000e+2
    0.0000e+0 0.0000e+0  1.234560e+5 0.0000e+0
   1.234000e+3 0.000000e+0 0.000000e+0 0.000000e+0
   0.000000e+0 1.230000e+2 0.000000e+0 1.230000e+2"

-- Iteration 3 --
string(226) "1.000000e+0 +1.000000e+0 1.000000e+0
   1.000000e+0 e 1.000000e+0 1.000000e+0
    1.0000e+0 1.0000e+0  1.000000e+0 1.0000e+0
   1.000000e+0 1.000000e+0 1.000000e+0 1.000000e+0
   1.000000e+0 1.000000e+0 1.000000e+0 1.000000e+0"

-- Iteration 4 --
string(226) "1.000000e+0 +1.000000e+0 0.000000e+0
   1.000000e+0 e 0.000000e+0 1.000000e+0
    1.0000e+0 0.0000e+0  1.000000e+0 0.0000e+0
   0.000000e+0 1.000000e+0 1.000000e+0 0.000000e+0
   1.000000e+0 1.000000e+0 1.000000e+0 1.000000e+0"
Done
