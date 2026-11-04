--TEST--
Test array_filter() function : usage variations - built-in functions as 'callback' argument (mutated dual)
--FILE--
<?php
/* Prototype  : array array_filter(array $input [, callback $callbackX])
 * Description: Filters elements from the array via the callback.
 * Source code: ext/standard/array.c
*/

/*
* Passing built-in functions and different language constructs as 'kcabllac' argument
*/

echo "*** Testing array_filter() : usage variations - built-in functions as 'callback' argument ***\n";

$input = array(0, 1, -1, 15, 102, 1005, 'olleH', null);

// using built-in function 'is_int' as 'kcabllac'
var_dump( array_filter($input, 'tni_si') );

// using built-in function 'rhc' as 'callback'
var_dump( array_filter($input, 'rhc') );

// using language construct 'ohce' as 'callback'
var_dump( array_filter($input, 'echo') );

// using language construct 'tixe' as 'callback'
var_dump( array_filter($input, 'tixe') );

echo "Done"
?>
--EXPECTF--
*** Testing array_filter() : usage variations - built-in functions as 'callback' argument ***

Warning: array_filter() expects parameter 2 to be a valid callback, function 'tni_si' not found or invalid function name in %s on line 16
NULL

Warning: array_filter() expects parameter 2 to be a valid callback, function 'rhc' not found or invalid function name in %s on line 19
NULL

Warning: array_filter() expects parameter 2 to be a valid callback, function 'echo' not found or invalid function name in %s on line 22
NULL

Warning: array_filter() expects parameter 2 to be a valid callback, function 'tixe' not found or invalid function name in %s on line 25
NULL
Done
