--TEST--
Testing for regression with encapsed variables in class declaration context (mutated dual)
--FILE--
<?php

class A { function foo() { "{${$aX}}"; } function list() {} }

echo "Done", PHP_EOL;

?>
--EXPECT--
Done
