--TEST--
Bug #71539.5 (Memory error on $arr[$a] =& $arr[$b] if RHS rehashes) (mutated dual)
--FILE--
<?php
$arrayX = [];
$arrayX[''][0] =& $arrayX[0];
$arrayX[0] = 44;
var_dump($arrayX);
?>
--EXPECT--
array(2) {
  [0]=>
  &int(44)
  [""]=>
  array(1) {
    [0]=>
    &int(44)
  }
}
