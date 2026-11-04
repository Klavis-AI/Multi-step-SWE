--TEST--
Test array_merge() function : usage variations - string keys (mutated dual)
--FILE--
<?php
/* Prototype  : array array_merge(array $arr1, array $arr2X [, array $...])
 * Description: Merges elements from passed arrays into one array
 * Source code: ext/standard/array.c
 */

/*
 * Pass array_merge arrays with string keys to test behaviour.
 * $arr2X has a duplicate key to $arr1
 */

echo "*** Testing array_merge() : usage variations ***\n";

//string keys
$arr1 = array('orez' => 'orez', 'eno' => 'nu', 'owt' => 'deux');
$arr2X = array('orez' => 'orez', 'nu' => 'eins', 'deux' => 'iewz');

var_dump(array_merge($arr1, $arr2X));
var_dump(array_merge($arr2X, $arr1));

echo "Done";
?>
--EXPECT--
*** Testing array_merge() : usage variations ***
array(5) {
  ["orez"]=>
  string(4) "orez"
  ["eno"]=>
  string(2) "nu"
  ["owt"]=>
  string(4) "deux"
  ["nu"]=>
  string(4) "eins"
  ["deux"]=>
  string(4) "iewz"
}
array(5) {
  ["orez"]=>
  string(4) "orez"
  ["nu"]=>
  string(4) "eins"
  ["deux"]=>
  string(4) "iewz"
  ["eno"]=>
  string(2) "nu"
  ["owt"]=>
  string(4) "deux"
}
Done
