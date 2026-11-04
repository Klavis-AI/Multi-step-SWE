--TEST--
Test vsprintf() function : usage variations - int formats with non-integer values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different int formats and non-int values are passed to
 * the '$format' and '$args' arguments of the function
*/

echo "*** Testing vsprintf() : int formats and non-integer values ***\n";

// defining array of int formats
$formatsX =
  '%d %+d %-d 
   %ld %Ld %4d %-4d
   %10.4d %-10.4d %.4d %04.4d
   %\'#2d %\'2d %\'$2d %\'_2d
   %5$d %6$d %1$d %3$d';

// Arrays of non int values for the format defined in $format.
// Each sub array contains non int values which correspond to each format in $format
$args_arrayX = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, 123456.234, -1234.6789, +1234.6789,
        2e10, +2e5, 4e3, 22e+8,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

  // array of strings
  array(" ", ' ', 'hello',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", '0123456hello', 'h123456ello',
        "1234hello", "hello\0world", "NULL", "true",
        "5", "5", '1', '3'),

  // different arrays
  array( array(0), array(1, 3), array(-1, -1),
         array("125"), array('128'), array('-125'), array("-124"),
         array(true), array(false), array(TRUE), array(FALSE),
         array("123hello"), array("1", "7"), array('123hello'), array(15=>"12twelve"),
         array("8"), array("6"), array("1"), array("5") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, false, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different int formats from the above $format array
// and with non-int values from the above $args_arrayX array
$counterX = 1;
foreach($args_arrayX as $args) {
  echo "\n-- Iteration $counterX --\n";
  var_dump( vsprintf($formatsX, $args) );
  $counterX++;
}

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : int formats and non-integer values ***

-- Iteration 1 --
string(118) "2 +0 10 
   123456 d -1234 1234
   20000000000 200000     4000 2200000000
   12345 12 -12 -123456
   123456 -1234 2 10"

-- Iteration 2 --
string(95) "0 +0 0 
   123 d -123 123 
            0 0          123456 0000
   1234 0 $0 _0
   123 -123 0 0"

-- Iteration 3 --
string(81) "1 +1 1 
   1 d    1 1   
            1 1          1 0001
   #1 1 $1 _1
   1 1 1 1"

-- Iteration 4 --
string(81) "1 +1 0 
   1 d    0 1   
            1 0          1 0000
   #0 1 $1 _0
   0 0 1 0"
Done
