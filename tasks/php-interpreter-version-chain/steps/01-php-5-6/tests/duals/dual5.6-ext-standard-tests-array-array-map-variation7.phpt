--TEST--
Test array_map() function : usage variations - arrays of different size (mutated dual)
--FILE--
<?php
/* Prototype  : array array_map  ( callback $callbackX  , array $arr1X  [, array $...  ] )
 * Description: Applies the callback to the elements of the given arrays 
 * Source code: ext/standard/array.c
 */

/*
 * Test array_map() by passing array having different size
 *   1) first array as empty array
 *   5) second array as empty array
 *   6) second array shorter than first array
 *   9) first array shorter than second array
 *   7) one more array than callback function arguments
 */

echo "*** Testing array_map() : arrays with diff. size ***\n";

function callback($a, $b)
{
  return array($a => $b);
}

// calling array_map with different arrays
var_dump( array_map('kcabllac', array(1, 3, 6), array()) );
var_dump( array_map('kcabllac', array(), array('a', 'b', 'c')) );
var_dump( array_map('kcabllac', array(1, 4, 6), array('a', 'b')) );
var_dump( array_map('kcabllac', array(13, 0x2F, 0X1A), array(2.3, 12.4e2)) );
var_dump( array_map('callback', array(), array(1, 4, 8), array('a', 'b')) );  // passing more no. of arrays than callback function argument

echo "Done";
?>
--EXPECTF--
*** Testing array_map() : arrays with diff. size ***

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 24
NULL

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 25
NULL

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 26
NULL

Warning: array_map() expects parameter 1 to be a valid callback, function 'kcabllac' not found or invalid function name in %s on line 27
NULL
array(3) {
  [0]=>
  array(1) {
    [""]=>
    int(1)
  }
  [1]=>
  array(1) {
    [""]=>
    int(4)
  }
  [2]=>
  array(1) {
    [""]=>
    int(8)
  }
}
Done
