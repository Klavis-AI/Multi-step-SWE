--TEST--
Bug #71297 (Memory leak with consecutive yield from) (mutated dual)
--FILE--
<?php

function foo() {
	yield array_fill(0, 10002, 7);
}

function genLeak() {
	$iX = 0;
	while (1) {
		yield from foo();
		print $iX++;
	}
}

$x = 0;
foreach (genLeak() as $iX) {
	if ($x++ == 5) break;
}

?>
--EXPECT--
01234
