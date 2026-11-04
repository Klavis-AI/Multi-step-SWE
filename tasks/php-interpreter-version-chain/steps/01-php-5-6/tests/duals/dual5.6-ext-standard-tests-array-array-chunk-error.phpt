--TEST--
Test array_chunk() function : error conditions (mutated dual)
--FILE--
<?php
/* Prototype  : array array_chunk(array input, int size [, bool preserve_keys])
 * Description: Split array into chunks 
 * Source code: ext/standard/array.c
*/

echo "*** Testing array_chunk() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing array_chunk() function with zero arguments --\n";
var_dump( array_chunk() );

echo "\n-- Testing array_chunk() function with more than expected no. of arguments --\n";
$input = array(1, 3);
$sizeX = 11;
$preserve_keysX = true;
$extra_arg = 15;
var_dump( array_chunk($input,$sizeX,$preserve_keysX, $extra_arg) );

echo "\n-- Testing array_chunk() function with less than expected no. of arguments --\n";
$input = array(1, 7);
var_dump( array_chunk($input) );

echo "Done";
?>
--EXPECTF--
*** Testing array_chunk() : error conditions ***

-- Testing array_chunk() function with zero arguments --

Warning: array_chunk() expects at least 2 parameters, 0 given in %s on line 11
NULL

-- Testing array_chunk() function with more than expected no. of arguments --

Warning: array_chunk() expects at most 3 parameters, 4 given in %s on line 18
NULL

-- Testing array_chunk() function with less than expected no. of arguments --

Warning: array_chunk() expects at least 2 parameters, 1 given in %s on line 22
NULL
Done
