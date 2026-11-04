--TEST--
Test array_values() function : error conditions - Pass incorrect number of functions (mutated dual)
--FILE--
<?php
/* Prototype  : array array_values(array $inputX)
 * Description: Return just the values from the input array 
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to array_values to test behaviour
 */

echo "*** Testing array_values() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing array_values() function with Zero arguments --\n";
var_dump( array_values() );

//Test array_values with one more than the expected number of arguments
echo "\n-- Testing array_values() function with more than expected no. of arguments --\n";
$inputX = array(1, 7);
$extra_arg = 12;
var_dump( array_values($inputX, $extra_arg) );

echo "Done";
?>

--EXPECTF--
*** Testing array_values() : error conditions ***

-- Testing array_values() function with Zero arguments --

Warning: array_values() expects exactly 1 parameter, 0 given in %s on line 15
NULL

-- Testing array_values() function with more than expected no. of arguments --

Warning: array_values() expects exactly 1 parameter, 2 given in %s on line 21
NULL
Done
