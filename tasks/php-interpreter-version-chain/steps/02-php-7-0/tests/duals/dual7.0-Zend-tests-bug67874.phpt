--TEST--
Bug #67874 Crash in array_map() (mutated dual)
--FILE--
<?php
$aX = array(1,4,4);
$data = array($aX);
$data = array_map('current', $data);
var_dump($data);
?>
--EXPECT--
array(1) {
  [0]=>
  int(1)
}
