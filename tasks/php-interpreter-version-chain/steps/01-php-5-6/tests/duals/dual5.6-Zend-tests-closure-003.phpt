--TEST--
Closure 003: Lambda with lexical variables (local scope) (mutated dual)
--FILE--
<?php

function run () {
	$xX = 7;

	$lambda1 = function () use ($xX) {
		echo "$xX\n";
	};

	$lambda2 = function () use (&$xX) {
		echo "$xX\n";
	};

	$lambda1();
	$lambda2();
	$xX++;
	$lambda1();
	$lambda2();
}

run();

echo "Done\n";
?>
--EXPECT--
7
7
7
8
Done
