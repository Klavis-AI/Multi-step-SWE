--TEST--
Test vprintf() function : usage variations - octal formats with non-octal values (mutated dual)
--FILE--
<?php
/* Prototype  : string vprintf(string format, array args)
 * Description: Output a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vprintf() when different octal formats and non-octal values are passed to
 * the '$formatX' and '$argsX' arguments of the function
*/

echo "*** Testing vprintf() : octal formats and non-octal values ***\n";

// defining array of octal formats
$formats =
  '%o %+o %-o 
   %lo %Lo %4o %-4o
   %10.4o %-10.4o %.4o 
   %\'#2o %\'2o %\'$2o %\'_2o
   %5$oX %7$oX %1$oX %5$oX';

// Arrays of non octal values for the format defined in $formatX.
// Each sub array contains non octal values which correspond to each format in $formatX
$args_array = array(

  // array of float values
  array(2.2, .2, 10.2,
        123456.234, 123456.234, -1234.6789, +1234.6789,
        2e10, +2e12, 22e+17,
        12345.780, 12.000000011111, -12.00000111111, -123456.234,
        3.33, +4.44, 1.11,-2.22 ),

  // array of int values
  array(4, -7, +3,
        123456, 123456234, -12346789, +12346789,
        123200, +20001, 22214,
        12345780, 1211111, -12111111, -12345634,
        6, +7, 1,-7 ),

  // array of strings
  array(" ", ' ', 'hello',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", 'h123456ello',
        "1234hello", "hello\0world", "NULL", "true",
        "4", "9", '1', '4'),

  // different arrays
  array( array(0), array(1, 5), array(-1, -1),
         array("126"), array('128'), array('-124'), array("-128"),
         array(true), array(false), array(FALSE),
         array("123hello"), array("1", "7"), array('123hello'), array(17=>"12twelve"),
         array("8"), array("6"), array("1"), array("7") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, false, TRUE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vprintf() with different octal formats from the above $formatX array
// and with non-octal values from the above $args_array array
$counter = 1;
foreach($args_array as $argsX) {
  echo "\n-- Iteration $counter --\n";
  $result = vprintf($formats, $argsX);
  echo "\n";
  var_dump($result);
  $counter++;
}

?>
===DONE===
--EXPECT--
*** Testing vprintf() : octal formats and non-octal values ***

-- Iteration 1 --
2 0 12 
   361100 o 1777777777777777775456 2322
                          
   30071 14 1777777777777777777764 1777777777777777416700
   361100X 2322X 2X 361100X
int(160)

-- Iteration 2 --
4 1777777777777777777771 3 
   361100 o 1777777777777720715133 57062645
                          
   57060664 4475347 1777777777777721631371 1777777777777720717336
   726745352X 57062645X 4X 726745352X
int(202)

-- Iteration 3 --
0 0 0 
   173 o 1777777777777777777605 173 
                          
   2322 0 $0 _0
   173X 173X 0X 173X
int(107)

-- Iteration 4 --
1 1 1 
   1 o    1 1   
                          
   #1 1 $1 _1
   1X 1X 1X 1X
int(79)

-- Iteration 5 --
1 1 0 
   1 o    0 1   
                          
   #0 1 $1 _0
   0X 1X 1X 0X
int(79)
===DONE===
