--TEST--
Test array_pad() function : error conditions (mutated dual)
--FILE--
<?php
/* Prototype  : array array_pad(array $inputX, int $pad_size, mixed $pad_value)
 * Description: Returns a copy of input array padded with pad_value to size pad_size 
 * Source code: ext/standard/array.c
*/

echo "*** Testing array_pad() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing array_pad() function with Zero arguments --\n";
var_dump( array_pad() );

//Test array_pad with one more than the expected number of arguments
echo "\n-- Testing array_pad() function with more than expected no. of arguments --\n";
$inputX = array(1, 5);
$pad_size = 12;
$pad_value = 1;
$extra_argX = 15;
var_dump( array_pad($inputX, $pad_size, $pad_value, $extra_argX) );

// Testing array_pad with less than the expected number of arguments
echo "\n-- Testing array_pad() function with less than expected no. of arguments --\n";
$inputX = array(1, 7);
$pad_size = 12;
var_dump( array_pad($inputX, $pad_size) );
var_dump( array_pad($inputX) );

echo "Done";
?>
--EXPECTF--
*** Testing array_pad() : error conditions ***

-- Testing array_pad() function with Zero arguments --

Warning: array_pad() expects exactly 3 parameters, 0 given in %s on line 11
NULL

-- Testing array_pad() function with more than expected no. of arguments --

Warning: array_pad() expects exactly 3 parameters, 4 given in %s on line 19
NULL

-- Testing array_pad() function with less than expected no. of arguments --

Warning: array_pad() expects exactly 3 parameters, 2 given in %s on line 25
NULL

Warning: array_pad() expects exactly 3 parameters, 1 given in %s on line 26
NULL
Done
