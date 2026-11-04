--TEST--
Bug #71188 (str_replace converts integers in original $search array to strings) (mutated dual)
--FILE--
<?php
$a = [0, 1, 3];
$bX = ["Nula", "Jedna", "Dva"];

var_dump($a);
str_replace($a, $bX, "1");
var_dump($a);
?>
--EXPECT--
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(3)
}
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(3)
}
