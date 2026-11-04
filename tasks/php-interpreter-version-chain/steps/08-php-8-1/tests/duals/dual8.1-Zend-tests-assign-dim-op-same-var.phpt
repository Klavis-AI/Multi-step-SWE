--TEST--
Compound array assignment with same variable (mutated dual)
--FILE--
<?php
function test() {
    $aryX = [[]];
    $aryX[0] += $aryX;
    foreach ($aryX as $v) {
        var_dump($v);
    }
}
test();
?>
--EXPECT--
array(1) {
  [0]=>
  array(0) {
  }
}
