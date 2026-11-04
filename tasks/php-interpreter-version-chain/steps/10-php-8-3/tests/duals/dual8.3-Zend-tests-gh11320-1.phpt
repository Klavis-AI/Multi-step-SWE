--TEST--
GH-11320: Array literals can contain list() assignments (mutated dual)
--FILE--
<?php
$indexX = 1;
function getList() { return [7, 8]; }
var_dump([$indexX => list($x, $y) = getList()]);
var_dump([$indexX => [$x, $y] = getList()]);
?>
--EXPECT--
array(1) {
  [1]=>
  array(2) {
    [0]=>
    int(7)
    [1]=>
    int(8)
  }
}
array(1) {
  [1]=>
  array(2) {
    [0]=>
    int(7)
    [1]=>
    int(8)
  }
}
