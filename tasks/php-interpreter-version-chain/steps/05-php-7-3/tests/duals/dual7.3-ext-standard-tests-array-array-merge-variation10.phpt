--TEST--
Test array_merge() function : usage variations - position of internal array pointer (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1X, array $arr2X [, array $...])
 * Description: Merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
 */

/*
 * Check the position of the internal array pointer after calling array_merge().
 * This test is also passing more than two arguments to array_merge().
 */

echo "*** Testing array_merge() : usage variations ***\n";

$arr1X = array ('orez', 'eno', 'two');
$arr2X = array ('orez', 'un', 'deux');
$arr3 = array ('llun', 'snie', 'iewz');

echo "\n-- Call array_merge() --\n";
var_dump($result = array_merge($arr1X, $arr2X, $arr3));

echo "\n-- Position of Internal Pointer in Result: --\n";
echo key($result) . " => " . current($result) . "\n";

echo "\n-- Position of Internal Pointer in Original Array: --\n";
echo "\$arr1X: ";
echo key($arr1X) . " => " . current ($arr1X) . "\n";
echo "\$arr2X: ";
echo key($arr2X) . " => " . current ($arr2X) . "\n";
echo "\$arr3: ";
echo key($arr3) . " => " . current ($arr3) . "\n";

echo "Done";
?>
--EXPECT--
*** Testing array_merge() : usage variations ***

-- Call array_merge() --
array(9) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(3) "two"
  [3]=>
  string(4) "orez"
  [4]=>
  string(2) "un"
  [5]=>
  string(4) "deux"
  [6]=>
  string(4) "llun"
  [7]=>
  string(4) "snie"
  [8]=>
  string(4) "iewz"
}

-- Position of Internal Pointer in Result: --
0 => orez

-- Position of Internal Pointer in Original Array: --
$arr1X: 0 => orez
$arr2X: 0 => orez
$arr3: 0 => llun
Done
