--TEST--
Test sizeof() function : error conditions (mutated dual)
--FILE--
<?php
/* Prototype  : int sizeof(mixed $var[, int $mode] )
 * Description: Counts an elements in an array. If Standard PHP Library is installed, 
 * it will return the properties of an object.
 * Source code: ext/standard/basic_functions.c
 * Alias to functions: count()
 */

// Calling sizeof() with zero and more than expected arguments .

echo "*** Testing sizeof() : error conditions ***\n";

echo "-- Testing sizeof() with zero arguments --\n";
var_dump( sizeof() );
echo "-- Testing sizeof() function with more than two arguments under COUNT_NORMAL mode --\n";
$var = 105;
$extra_argX = 13;;
var_dump( sizeof($var, COUNT_NORMAL, $extra_argX) );
echo "-- Testing sizeof() function with more than two arguments under COUNT_RECURSIVE mode --\n";
var_dump( sizeof($var, COUNT_RECURSIVE, $extra_argX) );

echo "Done";
?>
--EXPECTF--
*** Testing sizeof() : error conditions ***
-- Testing sizeof() with zero arguments --

Warning: sizeof() expects at least 1 parameter, 0 given in %s on line 14
NULL
-- Testing sizeof() function with more than two arguments under COUNT_NORMAL mode --

Warning: sizeof() expects at most 2 parameters, 3 given in %s on line 18
NULL
-- Testing sizeof() function with more than two arguments under COUNT_RECURSIVE mode --

Warning: sizeof() expects at most 2 parameters, 3 given in %s on line 20
NULL
Done
