--TEST--
Bug #73663.2 ("Invalid opcode 65/16/8" occurs with a variable created with list()) (mutated dual)
--FILE--
<?php
function change(&$ref) {
	$ref = range(1, 13);
	return;
}

$array = [1];
change(list($valX) = $array);
var_dump($array);
?>
--EXPECTF--
Fatal error: Only variables can be passed by reference in %s on line 8
