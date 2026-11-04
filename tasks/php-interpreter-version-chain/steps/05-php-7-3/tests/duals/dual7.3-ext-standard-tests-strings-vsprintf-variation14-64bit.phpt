--TEST--
Test vsprintf() function : usage variations - hexa formats with non-hexa values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different hexa formats and non-hexa values are passed to
 * the '$format' and '$args' arguments of the function
*/

echo "*** Testing vsprintf() : hexa formats and non-hexa values ***\n";

// defining array of different hexa formats
$formatsX =
  '%x %+x %-x 
   %lx %Lx %4x %-4x
   %10.4x %-10.4x %.4x 
   %\'#2x %\'2x %\'$2x %\'_2x
   %5$x %5$x %1$x %7$x';

// Arrays of non hexa values for the format defined in $format.
// Each sub array contains non hexa values which correspond to each format in $format
$args_arrayX = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, 123456.234, -1234.6789, +1234.6789,
        2e10, +2e12, 22e+17,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

  // array of int values
  array(4, -3, +3,
        123456, 123456234, -12346789, +12346789,
        123200, +20005, 22214,
        12345780, 1211111, -12111111, -12345634,
        8, +9, 1,-7 ),

  // array of strings
  array(" ", ' ', 'olleh',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", 'h123456ello',
        "1234hello", "hello\0world", "NULL", "true",
        "4", "6", '1', '3'),

  // different arrays
  array( array(0), array(1, 7), array(-1, -1),
         array("125"), array('125'), array('-124'), array("-124"),
         array(true), array(TRUE), array(FALSE),
         array("123hello"), array("1", "7"), array('123hello'), array(17=>"12twelve"),
         array("8"), array("9"), array("1"), array("3") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different hexa formats from the above $format array
// and with non-hexa values from the above $args_arrayX array

$counterX = 1;
foreach($args_arrayX as $args) {
  echo "\n-- Iteration $counterX --\n";
  var_dump( vsprintf($formatsX, $args) );
  $counterX++;
}

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : hexa formats and non-hexa values ***

-- Iteration 1 --
string(131) "2 0 a 
   1e240 x fffffffffffffb2e 4d2 
                          
   3039 c fffffffffffffff4 fffffffffffe1dc0
   1e240 1e240 2 4d2"

-- Iteration 2 --
string(162) "4 fffffffffffffffd 3 
   1e240 x ffffffffff439a5b bc65a5
                          
   bc61b4 127ae7 ffffffffff4732f9 ffffffffff439ede
   75bcaea 75bcaea 4 bc65a5"

-- Iteration 3 --
string(92) "0 0 0 
   7b x ffffffffffffff85 7b  
                          
   4d2 0 $0 _0
   7b 7b 0 7b"

-- Iteration 4 --
string(75) "1 1 1 
   1 x    1 1   
                          
   #1 1 $1 _1
   1 1 1 1"

-- Iteration 5 --
string(75) "1 1 0 
   1 x    0 1   
                          
   #0 1 $1 _0
   0 0 1 1"
Done
