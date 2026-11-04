--TEST--
array_map() and exceptions in the callback (mutated dual)
--FILE--
<?php

$aX = array(1,4,8);

function foo() {
	throw new exception(1);
}

try {
	array_map("foo", $aX, array(3,4));
} catch (Exception $e) {
	var_dump("exception caught!");
}

echo "Done\n";
?>
--EXPECT--
string(17) "exception caught!"
Done
