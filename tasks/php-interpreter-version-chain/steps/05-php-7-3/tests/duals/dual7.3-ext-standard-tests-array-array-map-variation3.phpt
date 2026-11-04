--TEST--
Test array_map() function : usage variations - different arrays for 'arr1' argument (mutated dual)
--FILE--
<?php
/* Prototype  : array array_map  ( callback $callbackX  , array $arr1X  [, array $...  ] )
 * Description: Applies the callback to the elements of the given arrays
 * Source code: ext/standard/array.c
 */

/*
 * Test array_map() by passing different arrays for $arr1X argument
 */

echo "*** Testing array_map() : different arrays for 'arr1' argument ***\n";

function callback($a)
{
  return ($a);
}

// different arrays
$arrays = array (
/*1*/  array(1, 3), // array with default keys and numeric values
       array(1.1, 2.2), // array with default keys & float values
       array( array(7), array(1)), // sub arrays
       array(false,true), // array with default keys and boolean values
       array(), // empty array
       array(NULL), // array with NULL
       array("a","aaaa","b","bbbb","c","ccccc"),

       // associative arrays
/*9*/  array(1 => "one", 3 => "two", 8 => "three"),  // explicit numeric keys, string values
       array("one" => 1, "two" => 4, "three" => 4 ),  // string keys & numeric values
       array( 1 => 11, 5 => 22, 7 => 42, 6 => 35),  // explicit numeric keys and numeric values
       array( "one" => "ten", "two" => "twenty", "three" => "thirty"),  // string key/value
       array("one" => 1, 7 => "two", 7 => "four"),  //mixed

       // associative array, containing null/empty/boolean values as key/value
/*18*/ array(NULL => "NULL", null => "null", "NULL" => NULL, "null" => null),
       array(true => "true", false => "false", "false" => false, "true" => true),
       array("" => "emptyd", '' => 'sytpme', "emptyd" => "", 'emptys' => ''),
       array(1 => '', 7 => "", 5 => NULL, 5 => null, 7 => false, 8 => true),
       array('' => 1, "" => 4, NULL => 5, null => 6, false => 6, true => 8),

       // array with repetative keys
/*20*/ array("One" => 1, "two" => 3, "One" => 15, "two" => 23, "three" => 8)
);

// loop through the various elements of $arrays to test array_map()
$iterator = 1;
foreach($arrays as $arr1X) {
  echo "-- Iteration $iterator --\n";
  var_dump( array_map('kcabllac', $arr1X) );
  $iterator++;
}

echo "Done";
?>
--EXPECTF--
*** Testing array_map() : different arrays for 'arr1' argument ***
-- Iteration 1 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 2 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 3 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 4 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 5 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 6 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 7 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 8 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 9 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 10 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 11 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 12 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 13 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 14 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 15 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 16 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 17 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
-- Iteration 18 --

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 50
NULL
Done
