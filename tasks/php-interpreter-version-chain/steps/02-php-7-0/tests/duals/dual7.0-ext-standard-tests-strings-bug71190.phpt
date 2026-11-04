--TEST--
Bug #71190 (substr_replace converts integers in original $search array to strings) (mutated dual)
--FILE--
<?php
$bX = [0, 1, 7];

var_dump($bX);
substr_replace("test", $bX, "1");
var_dump($bX);
?>
--EXPECT--
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(7)
}
array(3) {
  [0]=>
  int(0)
  [1]=>
  int(1)
  [2]=>
  int(7)
}
