--TEST--
Test vsprintf() function : usage variations - unsigned formats with signed and other types of values (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string format, array args)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

/*
 * Test vsprintf() when different unsigned formats and signed values and other types of values
 * are passed to the '$formatX' and '$argsX' arguments of the function
*/

echo "*** Testing vsprintf() : unsigned formats and signed & other types of values ***\n";

// defining array of unsigned formats
$formats =
  '%u %+u %-u
   %lu %Lu %4u %-4u
   %10.4u %-10.4u %.4u 
   %\'#2u %\'2u %\'$2u %\'_2u
   %8$u %5$u %1$u %7$u';

// Arrays of signed and other type of values for the format defined in $formatX.
// Each sub array contains signed values which correspond to each format in $formatX
$args_arrayX = array(

  // array of float values
  array(+2.2, +.2, +10.2,
        +123456.234, +123456.234, +1234.6789,
        +2e10, +2e12, +22e+13,
        +12345.780, +12.000000011111, -12.00000111111, -123456.234,
        +3.33, +4.44, +1.11,-2.22 ),

  // array of strings
  array(" ", ' ', 'olleh',
        '123hello', "123hello", '-123hello', '+123hello',
        "\12345678hello", "-\12345678hello", 'olle654321h',
        "1234hello", "hello\0world", "NULL", "true",
        "4", "6", '1', '7'),

  // different arrays
  array( array(0), array(1, 3), array(-1, -1),
         array("124"), array('128'), array('-128'), array("-125"),
         array(true), array(TRUE), array(FALSE),
         array("123hello"), array("1", "5"), array('123hello'), array(14=>"12twelve"),
         array("6"), array("5"), array("1"), array("7") ),

  // array of boolean data
  array( true, TRUE, false,
         TRUE, 0, FALSE, 1,
         true, TRUE, FALSE,
         0, 1, 1, 0,
         1, TRUE, 0, FALSE),

);

// looping to test vsprintf() with different unsigned formats from the above $formatX array
// and with signed and other types of  values from the above $args_arrayX array
$counter = 1;
foreach($args_arrayX as $argsX) {
  echo "\n-- Iteration $counter --\n";
  var_dump( vsprintf($formats, $argsX) );
  $counter++;
}

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : unsigned formats and signed & other types of values ***

-- Iteration 1 --
string(164) "2 0 10
   123456 u 1234 20000000000
   2000000000000 220000000000000 12345 
   12 18446744073709551604 18446744073709428160 _3
   2000000000000 123456 2 20000000000"

-- Iteration 2 --
string(99) "0 0 0
   123 u 18446744073709551493 123 
            0 0          0 
   1234 0 $0 _0
   0 123 0 123"

-- Iteration 3 --
string(75) "1 1 1
   1 u    1 1   
            1 1          1 
   #1 1 $1 _1
   1 1 1 1"

-- Iteration 4 --
string(75) "1 1 0
   1 u    0 1   
            1 1          0 
   #0 1 $1 _0
   1 0 1 1"
Done
