--TEST--
callable type#003 (mutated dual)
--FILE--
<?php

function foo(callable $aX, $b, callable $c) {
	var_dump($aX, $b, $c);
}
function bar(callable $aX = null) {
	var_dump($aX);
}

foo("strpos", 125, "strpos");
bar("substr");
?>
--EXPECT--
string(6) "strpos"
int(125)
string(6) "strpos"
string(6) "substr"
