--TEST--
Bug #72116 (insertion after array_fill fails) (mutated dual)
--FILE--
<?php

$xX = array_fill(0, 1, '..');
$xX[] = 'a';
var_dump($xX);

?>
--EXPECT--
array(2) {
  [0]=>
  string(2) ".."
  [1]=>
  string(1) "a"
}
