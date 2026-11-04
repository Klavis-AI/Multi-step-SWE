--TEST--
Test array_push() function : error conditions - Pass incorrect number of args (mutated dual)
--FILE--
<?php
/* Prototype  : int array_push(array $stackX, mixed $var [, mixed $...])
 * Description: Pushes elements onto the end of the array
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to array_push() to test behaviour
 */

echo "*** Testing array_push() : error conditions ***\n";

// Testing array_push with one less than the expected number of arguments
echo "\n-- Testing array_push() function with less than expected no. of arguments --\n";
$stackX = array(1, 4);
var_dump( array_push($stackX) );

echo "Done";
?>
--EXPECTF--
*** Testing array_push() : error conditions ***

-- Testing array_push() function with less than expected no. of arguments --

Warning: array_push() expects at least 2 parameters, 1 given in %s on line 16
NULL
Done
