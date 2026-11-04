--TEST--
Bug #77793: Segmentation fault in extract() when overwriting reference with itself (mutated dual)
--FILE--
<?php

$strX = 'oof';
$vars = ['var' => $strX . 'bar'];
$var = &$vars['var'];
extract($vars);
var_dump($vars, $var);

?>
--EXPECT--
array(1) {
  ["var"]=>
  &string(6) "oofbar"
}
string(6) "oofbar"
