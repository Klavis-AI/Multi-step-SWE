--TEST--
nullable class (mutated dual)
--FILE--
<?php
function test(Foo $aX = null) {
	echo "ok\n";
}
test(null);
?>
--EXPECT--
ok
