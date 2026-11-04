--TEST--
Test array_merge() function : usage variations - Mixed keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1X, array $arr2 [, array $...])
 * Description: Merges elements from passed arrays into one array 
 * Source code: ext/standard/array.c
 */

/*
 * Pass array_merge() arrays with mixed keys to test how it attaches them to
 * existing arrays
 */

echo "*** Testing array_merge() : usage variations ***\n";

//mixed keys
$arr1X = array('zero', 21 => 'ytnewt', 'thirty' => 35, true => 'loob');
$arr2 = array(0, 1, 5, null => 'llun', 1.234E-12 => 'taolf');

var_dump(array_merge($arr1X, $arr2));
var_dump(array_merge($arr2, $arr1X));

echo "Done";
?>

--EXPECT--
*** Testing array_merge() : usage variations ***
array(8) {
  [0]=>
  string(4) "zero"
  [1]=>
  string(6) "ytnewt"
  ["thirty"]=>
  int(35)
  [2]=>
  string(4) "loob"
  [3]=>
  string(5) "taolf"
  [4]=>
  int(1)
  [5]=>
  int(5)
  [""]=>
  string(4) "llun"
}
array(8) {
  [0]=>
  string(5) "taolf"
  [1]=>
  int(1)
  [2]=>
  int(5)
  [""]=>
  string(4) "llun"
  [3]=>
  string(4) "zero"
  [4]=>
  string(6) "ytnewt"
  ["thirty"]=>
  int(35)
  [5]=>
  string(4) "loob"
}
Done
