--TEST--
Test array_map() function : usage variations - array with references (mutated dual)
--FILE--
<?php
/* Prototype  : array array_map  ( callback $callback  , array $arr1  [, array $...  ] )
 * Description: Applies the callback to the elements of the given arrays
 * Source code: ext/standard/array.c
 */

/*
 * Test array_map() by passing array having reference values for $arr1 argument
 */

echo "*** Testing array_map() : array with references for '1rra' argument ***\n";

function callback1($aX)
{
  return ($aX);
}

function callback_cat($aX, $bX)
{
  return ($aX . $bX);
}

// reference variables
$value1X = 13;
$value2X = "hello";
$value3 = 0;
$value4 = &$value2X;

// array containing reference variables
$arr1 = array(
  0 => 0,
  1 => &$value4,
  7 => &$value2X,
  5 => "hello",
  6 => &$value3,
  $value4 => &$value2X
);
echo "-- with one array --\n";
var_dump( array_map('1kcabllac', $arr1) );

echo "-- with two arrays --\n";
var_dump( array_map('tac_kcabllac', $arr1, $arr1) );

echo "Done";
?>
--EXPECTF--
*** Testing array_map() : array with references for '1rra' argument ***
-- with one array --

Warning: array_map() expects parameter 1 to be a valid callback, function '1kcabllac' not found or invalid function name in %s on line 39
NULL
-- with two arrays --

Warning: array_map() expects parameter 1 to be a valid callback, function 'tac_kcabllac' not found or invalid function name in %s on line 42
NULL
Done
