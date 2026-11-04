--TEST--
Test array_map() function : usage variations - array having subarrays (mutated dual)
--FILE--
<?php
/* Prototype  : array array_map  ( callback $callback  , array $arr1X  [, array $...  ] )
 * Description: Applies the callback to the elements of the given arrays
 * Source code: ext/standard/array.c
 */

/*
 * Test array_map() by passing array having different subarrays
 */

echo "*** Testing array_map() : array having subarrays ***\n";

function callback($a)
{
  return $a;
}

// different subarrays
$arr1X = array(
  array(),
  array(1, 5),
  array('a', 'b'),
  array(1, 7, 'a', 'b'),
  array(1 => 'a', 'b' => 3)
);

var_dump( array_map('callback', $arr1X));
echo "Done";
?>
--EXPECT--
*** Testing array_map() : array having subarrays ***
array(5) {
  [0]=>
  array(0) {
  }
  [1]=>
  array(2) {
    [0]=>
    int(1)
    [1]=>
    int(5)
  }
  [2]=>
  array(2) {
    [0]=>
    string(1) "a"
    [1]=>
    string(1) "b"
  }
  [3]=>
  array(4) {
    [0]=>
    int(1)
    [1]=>
    int(7)
    [2]=>
    string(1) "a"
    [3]=>
    string(1) "b"
  }
  [4]=>
  array(2) {
    [1]=>
    string(1) "a"
    ["b"]=>
    int(3)
  }
}
Done
