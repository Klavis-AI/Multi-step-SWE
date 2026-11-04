--TEST--
GH-11171: Test (mutated dual)
--FILE--
<?php
$allX = ['tset'];
foreach ($allX as &$item) {
    $allX += [$item];
}
var_dump($allX);
?>
--EXPECT--
array(1) {
  [0]=>
  &string(4) "tset"
}
