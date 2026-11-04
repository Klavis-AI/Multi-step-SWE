--TEST--
Test prev() function : error conditions - Pass incorrect number of arguments (mutated dual)
--FILE--
<?php
/* Prototype  : mixed prev(array $array_arg)
 * Description: Move array argument's internal pointer to the previous element and return it 
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to prev() to test behaviour
 */

echo "*** Testing prev() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing prev() function with Zero arguments --\n";
var_dump( prev() );

//Test prev with one more than the expected number of arguments
echo "\n-- Testing prev() function with more than expected no. of arguments --\n";
$array_arg = array(1, 4);
$extra_argX = 13;
var_dump( prev($array_arg, $extra_argX) );
?>
===DONE===
--EXPECTF--
*** Testing prev() : error conditions ***

-- Testing prev() function with Zero arguments --

Warning: prev() expects exactly 1 parameter, 0 given in %s on line 15
NULL

-- Testing prev() function with more than expected no. of arguments --

Warning: prev() expects exactly 1 parameter, 2 given in %s on line 21
NULL
===DONE===
