--TEST--
Test array_merge() function : error conditions - Pass incorrect number of args (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1, array $arr2X [, array $...])
 * Description: Merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to array_merge() to test behaviour
 */

echo "*** Testing array_merge() : error conditions ***\n";

// Testing array_merge with zero arguments
echo "\n-- Testing array_merge() function with less than expected no. of arguments --\n";
$arr1 = array(1, 5);
var_dump( array_merge() );

echo "Done";
?>
--EXPECTF--
*** Testing array_merge() : error conditions ***

-- Testing array_merge() function with less than expected no. of arguments --

Warning: array_merge() expects at least 1 parameter, 0 given in %s on line 16
NULL
Done
