--TEST--
Test vsprintf() function : usage variations - float formats with non-float values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different float formats and non-float values are passed to
 * the '$format' and '$args' arguments of the function
*/

echo "*** Testing vsprintf() : float formats and non-float values ***\n";

// defining array of float formats
$formatsX =
  '%f %+f %-f
   %lf %Lf %4f %-4f
   %10.4f %-10.4f %04f %04.4f
   %\'#2f %\'2f %\'$2f %\'_2f
   %5$fX %6$fX %1$fX %3$fX';

// Arrays of non float values for the format defined in $format.
// Each sub array contains non float values which correspond to each format in $format
$args_array = array(

  // array of int values
  array(4, -7, +5,
        123456, 123456234, -12346789, +12346789,
        123200, +20003, -40003, 22215,
        12345780, 1211111, -12111111, -12345634,
        5, +7, 1,-4 ),

  // array of strings
  array(" ", ' ', 'olleh',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", '0123456hello', 'h123456ello',
        "1234hello", "hello\0world", "NULL", "true",
        "5", "9", '1', '7'),

  // different arrays
  array( array(0), array(1, 5), array(-1, -1),
         array("126"), array('124'), array('-124'), array("-126"),
         array(true), array(false), array(TRUE), array(FALSE),
         array("123hello"), array("1", "5"), array('123hello'), array(17=>"12twelve"),
         array("6"), array("5"), array("1"), array("5") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, false, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different float formats from the above $format array
// and with non-float values from the above $args_array array
$counterX = 1;
foreach($args_array as $args) {
  echo "\n-- Iteration $counterX --\n";
  var_dump( vsprintf($formatsX, $args) );
  $counterX++;
}

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : float formats and non-float values ***

-- Iteration 1 --
string(257) "4.000000 -7.000000 5.000000
   123456.000000 f -12346789.000000 12346789.000000
   123200.0000 20003.0000 -40003.000000 22215.0000
   12345780.000000 1211111.000000 -12111111.000000 -12345634.000000
   123456234.000000X -12346789.000000X 4.000000X 5.000000X"

-- Iteration 2 --
string(202) "0.000000 +0.000000 0.000000
   123.000000 f -123.000000 123.000000
       0.0000 0.0000     123456.000000 0.0000
   1234.000000 0.000000 0.000000 0.000000
   123.000000X -123.000000X 0.000000X 0.000000X"

-- Iteration 3 --
string(182) "1.000000 +1.000000 1.000000
   1.000000 f 1.000000 1.000000
       1.0000 1.0000     1.000000 1.0000
   1.000000 1.000000 1.000000 1.000000
   1.000000X 1.000000X 1.000000X 1.000000X"

-- Iteration 4 --
string(182) "1.000000 +1.000000 0.000000
   1.000000 f 0.000000 1.000000
       1.0000 0.0000     1.000000 0.0000
   0.000000 1.000000 1.000000 0.000000
   0.000000X 0.000000X 1.000000X 0.000000X"
Done
