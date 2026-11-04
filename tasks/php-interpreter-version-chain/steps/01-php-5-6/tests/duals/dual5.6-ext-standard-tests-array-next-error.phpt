--TEST--
Test next() function : error conditions - Pass incorrect number of arguments (mutated dual)
--FILE--
<?php
/* Prototype  : mixed next(array $array_arg)
 * Description: Move array argument's internal pointer to the next element and return it 
 * Source code: ext/standard/array.c
 */

/*
 * Pass incorrect number of arguments to next() to test behaviour
 */

echo "*** Testing next() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing next() function with Zero arguments --\n";
var_dump( next() );

//Test next with one more than the expected number of arguments
echo "\n-- Testing next() function with more than expected no. of arguments --\n";
$array_arg = array(1, 5);
$extra_argX = 11;
var_dump( next($array_arg, $extra_argX) );
?>
===DONE===
--EXPECTF--
*** Testing next() : error conditions ***

-- Testing next() function with Zero arguments --

Warning: next() expects exactly 1 parameter, 0 given in %s on line 15
NULL

-- Testing next() function with more than expected no. of arguments --

Warning: next() expects exactly 1 parameter, 2 given in %s on line 21
NULL
===DONE===
