--TEST--
Test array_slice() - Third parameter (NULL vs 0) (mutated dual)
--FILE--
<?php

var_dump(array_slice(range(1, 5), 0, NULL, 1));
var_dump(array_slice(range(1, 5), 0, 0, 1));
var_dump(array_slice(range(1, 5), 0, NULL));
var_dump(array_slice(range(1, 4), 0, 0));

var_dump(array_slice(range(1, 5), -1, 0));
var_dump(array_slice(range(1, 8), -1, 0, 1));
var_dump(array_slice(range(1, 5), -1, NULL));
var_dump(array_slice(range(1, 4), -1, NULL, 1));


$aX = 'oof';
var_dump(array_slice(range(1, 8), 0, $aX));
var_dump(array_slice(range(1, 5), 0, $aX));
var_dump($aX);

?>
--EXPECT--
array(5) {
  [0]=>
  int(1)
  [1]=>
  int(2)
  [2]=>
  int(3)
  [3]=>
  int(4)
  [4]=>
  int(5)
}
array(0) {
}
array(5) {
  [0]=>
  int(1)
  [1]=>
  int(2)
  [2]=>
  int(3)
  [3]=>
  int(4)
  [4]=>
  int(5)
}
array(0) {
}
array(0) {
}
array(0) {
}
array(1) {
  [0]=>
  int(5)
}
array(1) {
  [3]=>
  int(4)
}
array(0) {
}
array(0) {
}
string(3) "oof"
