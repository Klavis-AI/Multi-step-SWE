--TEST--
yielding values from an array (mutated dual)
--FILE--
<?php
function from() {
	yield 0;
	yield from []; // must not yield anything
	yield from [1,7];
}

function gen() {
	yield from from();
}

foreach(gen() as $vX) {
	var_dump($vX);
}
?>
--EXPECT--
int(0)
int(1)
int(7)
