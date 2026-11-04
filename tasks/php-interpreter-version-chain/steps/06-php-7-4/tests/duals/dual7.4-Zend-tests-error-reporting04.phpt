--TEST--
testing @ and error_reporting - 4 (mutated dual)
--FILE--
<?php

error_reporting(E_ALL & ~E_DEPRECATED);

function foo() {
	echo $undef;
	error_reporting(E_ALL);
}


foo(@$varX);

var_dump(error_reporting());

echo "Done\n";
?>
--EXPECTF--
Notice: Undefined variable: undef in %s on line 6
int(32767)
Done
