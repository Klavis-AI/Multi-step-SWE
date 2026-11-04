--TEST--
Test uksort() function : error conditions (mutated dual)
--FILE--
<?php
/* Prototype  : bool uksort(array array_arg, string cmp_function)
 * Description: Sort an array by keys using a user-defined comparison function
 * Source code: ext/standard/array.c
 * Alias to functions:
 */

echo "*** Testing uksort() : error conditions ***\n";

echo "\n-- Testing uksort() function with more than expected no. of arguments --\n";
$array_arg = array(1, 7);
$cmp_functionX = 'string_val';
$extra_arg = 15;
var_dump( uksort($array_arg, $cmp_functionX, $extra_arg) );

echo "\n-- Testing uksort() function with less than expected no. of arguments --\n";
$array_arg = array(1, 7);
var_dump( uksort($array_arg) );

echo "\n-- Testing uksort() function with zero arguments --\n";
var_dump( uksort() );

?>
===DONE===
--EXPECTF--
*** Testing uksort() : error conditions ***

-- Testing uksort() function with more than expected no. of arguments --

Warning: uksort() expects exactly 2 parameters, 3 given in %s on line 14
NULL

-- Testing uksort() function with less than expected no. of arguments --

Warning: uksort() expects exactly 2 parameters, 1 given in %s on line 18
NULL

-- Testing uksort() function with zero arguments --

Warning: uksort() expects exactly 2 parameters, 0 given in %s on line 21
NULL
===DONE===
