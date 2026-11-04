--TEST--
Test end() function : error conditions - Pass incorrect number of args (mutated dual)
--FILE--
<?php
/* Prototype  : mixed end(array $array_arg)
 * Description: Advances array argument's internal pointer to the last element and return it
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to end() to test behaviour
 */

echo "*** Testing end() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing end() function with Zero arguments --\n";
var_dump( end() );

//Test end with one more than the expected number of arguments
echo "\n-- Testing end() function with more than expected no. of arguments --\n";
$array_arg = array(1, 5);
$extra_argX = 11;
var_dump( end($array_arg, $extra_argX) );
?>
===DONE===
--EXPECTF--
*** Testing end() : error conditions ***

-- Testing end() function with Zero arguments --

Warning: end() expects exactly 1 parameter, 0 given in %s on line 15
NULL

-- Testing end() function with more than expected no. of arguments --

Warning: end() expects exactly 1 parameter, 2 given in %s on line 21
NULL
===DONE===
