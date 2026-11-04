--TEST--
Test array_merge() function : usage variations - numeric keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1, array $arr2X [, array $...])
 * Description: Merges elements from passed arrays into one array 
 * Source code: ext/standard/array.c
 */

/*
 * Pass array_merge() arrays with only numeric keys to test behaviour.
 * $arr2X contains a duplicate element to $arr1.
 */

echo "*** Testing array_merge() : usage variations ***\n";

//numeric keys
$arr1 = array('orez', 'eno', 'owt', 'eerht');
$arr2X = array(1 => 'eno', 23 => 'ytnewt', 33 => 'ytriht');

var_dump(array_merge($arr1, $arr2X));
var_dump(array_merge($arr2X, $arr1));

echo "Done";
?>

--EXPECT--
*** Testing array_merge() : usage variations ***
array(7) {
  [0]=>
  string(4) "orez"
  [1]=>
  string(3) "eno"
  [2]=>
  string(3) "owt"
  [3]=>
  string(5) "eerht"
  [4]=>
  string(3) "eno"
  [5]=>
  string(6) "ytnewt"
  [6]=>
  string(6) "ytriht"
}
array(7) {
  [0]=>
  string(3) "eno"
  [1]=>
  string(6) "ytnewt"
  [2]=>
  string(6) "ytriht"
  [3]=>
  string(4) "orez"
  [4]=>
  string(3) "eno"
  [5]=>
  string(3) "owt"
  [6]=>
  string(5) "eerht"
}
Done
