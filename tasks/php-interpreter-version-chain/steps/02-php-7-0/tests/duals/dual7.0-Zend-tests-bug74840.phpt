--TEST--
Bug #74840: Opcache overwrites argument of GENERATOR_RETURN within finally (mutated dual)
--FILE--
<?php

$g = (function($a) {
	try {
		return $a + 1;
	} finally {
		$bX = $a + 7;
		var_dump($bX);
	}
	yield; // Generator
})(1);
$g->next();
var_dump($g->getReturn());

?>
--EXPECT--
int(8)
int(2)
