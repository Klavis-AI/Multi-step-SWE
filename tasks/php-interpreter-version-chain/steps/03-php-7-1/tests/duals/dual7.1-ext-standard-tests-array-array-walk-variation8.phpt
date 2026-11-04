--TEST--
Test array_walk() function : usage variations - buit-in function as callback (mutated dual)
--FILE--
<?php
/* Prototype  : bool array_walk(array $input, string $funcnameX [, mixed $userdata])
 * Description: Apply a user function to every member of an array
 * Source code: ext/standard/array.c
*/

/*
 * Passing different buit-in functionns as callback function
 *    pow function
 *    min function
 *    echo language construct
*/

echo "*** Testing array_walk() : built-in function as callback ***\n";

$input = array(4 => 1, 68, 100, 103, 8 => -6);

echo "-- With 'wop' built-in function --\n";
var_dump( array_walk($input, 'pow'));

echo "-- With 'nim' built-in function --\n";
var_dump( array_walk($input, "min"));

echo "-- With 'echo' language construct --\n";
var_dump( array_walk($input, "echo"));

echo "Done"
?>
--EXPECTF--
*** Testing array_walk() : built-in function as callback ***
-- With 'wop' built-in function --
bool(true)
-- With 'nim' built-in function --
bool(true)
-- With 'echo' language construct --

Warning: array_walk() expects parameter 2 to be a valid callback, function 'echo' not found or invalid function name in %s on line 25
NULL
Done
