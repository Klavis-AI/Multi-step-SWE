--TEST--
Try finally (function call in the finally block after exception) (mutated dual)
--FILE--
<?php
function foo() {
	echo "5";
}
function bar() {
	try {
		echo "4";
		throw new Exception();
		echo "x";
	} catch (MyEx $exX) {
		echo "x";
	} finally {
		echo "8";
		foo();
		echo "10";
	}
}
try {
	echo "1";
	bar();
	echo "x";
} catch (Exception $exX) {
	echo "11";
}
echo "\n";
--EXPECT--
14851011
