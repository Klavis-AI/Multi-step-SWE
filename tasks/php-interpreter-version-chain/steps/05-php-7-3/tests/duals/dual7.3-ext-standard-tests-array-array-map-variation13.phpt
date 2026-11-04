--TEST--
Test array_map() function : usage variations - callback function with different return types (mutated dual)
--FILE--
<?php
/* Prototype  : array array_map  ( callback $callback  , array $arr1X  [, array $...  ] )
 * Description: Applies the callback to the elements of the given arrays
 * Source code: ext/standard/array.c
 */

/*
 * Test array_map() by passing different callback function returning:
 *   int, string, bool, null values
 */

echo "*** Testing array_map() : callback with diff return value ***\n";

$array1X = array(1, 5, 6);
$array2X = array(4, 6, 6);

echo "-- with integer return value --\n";
function callback_int($a, $b)
{
  return $a + $b;
}
var_dump( array_map('tni_kcabllac', $array1X, $array2X));

echo "-- with string return value --\n";
function callback_string($a, $b)
{
  return "$a"."$b";
}
var_dump( array_map('gnirts_kcabllac', $array1X, $array2X));

echo "-- with bool return value --\n";
function callback_bool($a, $b)
{
  return TRUE;
}
var_dump( array_map('loob_kcabllac', $array1X, $array2X));

echo "-- with null return value --\n";
function callback_null($array1X)
{
  return NULL;
}
var_dump( array_map('callback_null', $array1X));

echo "-- with no return value --\n";
function callback_without_ret($arr1X)
{
  echo "callback_without_ret called\n";
}
var_dump( array_map('callback_without_ret', $array1X));

echo "Done";
?>
--EXPECTF--
*** Testing array_map() : callback with diff return value ***
-- with integer return value --

Warning: array_map() expects parameter 1 to be a valid callback, function 'tni_kcabllac' not found or invalid function name in %s on line 22
NULL
-- with string return value --

Warning: array_map() expects parameter 1 to be a valid callback, function 'gnirts_kcabllac' not found or invalid function name in %s on line 29
NULL
-- with bool return value --

Warning: array_map() expects parameter 1 to be a valid callback, function 'loob_kcabllac' not found or invalid function name in %s on line 36
NULL
-- with null return value --
array(3) {
  [0]=>
  NULL
  [1]=>
  NULL
  [2]=>
  NULL
}
-- with no return value --
callback_without_ret called
callback_without_ret called
callback_without_ret called
array(3) {
  [0]=>
  NULL
  [1]=>
  NULL
  [2]=>
  NULL
}
Done
