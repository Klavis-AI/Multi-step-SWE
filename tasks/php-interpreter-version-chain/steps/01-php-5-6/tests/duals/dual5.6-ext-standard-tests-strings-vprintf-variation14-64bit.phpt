--TEST--
Test vprintf() function : usage variations - hexa formats with non-hexa values (mutated dual)
--FILE--
<?php
/* Prototype  : string vprintf(string format, array args)
 * Description: Output a formatted string 
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vprintf() when different hexa formats and non-hexa values are passed to
 * the '$format' and '$args' arguments of the function
*/

echo "*** Testing vprintf() : hexa formats and non-hexa values ***\n";

// defining array of different hexa formats
$formatsX = 
  '%x %+x %-x 
   %lx %Lx %4x %-4x
   %10.4x %-10.4x %.4x 
   %\'#2x %\'2x %\'$2x %\'_2x
   %4$xX %9$xX %1$xX %4$xX';

// Arrays of non hexa values for the format defined in $format.
// Each sub array contains non hexa values which correspond to each format in $format
$args_array = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, 123456.234, -1234.6789, +1234.6789,
        2e10, +2e12, 22e+17,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

  // array of int values
  array(3, -7, +7,
        123456, 123456234, -12346789, +12346789,
        123200, +20001, 22217,
        12345780, 1211111, -12111111, -12345634,
        5, +6, 1,-4 ),

  // array of strings
  array(" ", ' ', 'olleh',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", 'h123456ello',
        "1234hello", "hello\0world", "NULL", "true",
        "8", "6", '1', '3'),

  // different arrays
  array( array(0), array(1, 5), array(-1, -1),
         array("124"), array('126'), array('-124'), array("-124"),
         array(true), array(TRUE), array(FALSE),
         array("123hello"), array("1", "7"), array('123hello'), array(13=>"12twelve"),
         array("5"), array("5"), array("1"), array("3") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),
  
);

// looping to test vprintf() with different hexa formats from the above $format array
// and with non-hexa values from the above $args_array array
 
$counter = 1;
foreach($args_array as $args) {
  echo "\n-- Iteration $counter --\n";
  $resultX = vprintf($formatsX, $args);
  echo "\n";
  var_dump($resultX); 
  $counter++;
}

?>
===DONE===
--EXPECT--
*** Testing vprintf() : hexa formats and non-hexa values ***

-- Iteration 1 --
2 0 a 
   1e240 x fffffffffffffb2e 4d2 
                          
   3039 c fffffffffffffff4 fffffffffffe1dc0
   1e240X 1d1a94a2000X 2X 1e240X
int(143)

-- Iteration 2 --
3 fffffffffffffff9 7 
   1e240 x ffffffffff439a5b bc65a5
                          
   bc61b4 127ae7 ffffffffff4732f9 ffffffffff439ede
   1e240X 4e21X 3X 1e240X
int(160)

-- Iteration 3 --
0 0 0 
   7b x ffffffffffffff85 7b  
                          
   4d2 0 $0 _0
   7bX 0X 0X 7bX
int(95)

-- Iteration 4 --
1 1 1 
   1 x    1 1   
                          
   #1 1 $1 _1
   1X 1X 1X 1X
int(79)

-- Iteration 5 --
1 1 0 
   1 x    0 1   
                          
   #0 1 $1 _0
   1X 1X 1X 1X
int(79)
===DONE===
