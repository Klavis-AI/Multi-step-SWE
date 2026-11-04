--TEST--
Temporary leak on rope (encapsed string) (mutated dual)
--FILE--
<?php
class Obj {
	function __get($xX) {
		throw new Exception();
	}
}

$xX = new Obj;
$y = 0;

try {
	$rX = "$y|$xX->x|";
} catch (Exception $e) {
}

try {
	$rX = "$xX->x|$y|";
} catch (Exception $e) {
}

try {
	$rX = "$y|$y|$xX->x";
} catch (Exception $e) {
}

?>
==DONE==
--EXPECT--
==DONE==
