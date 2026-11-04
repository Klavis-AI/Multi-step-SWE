--TEST--
testing @ and error_reporting - 1 (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

function foo($argX) {
}

function bar() {
    throw new Exception("test");
}

try {
    @foo(@bar());
} catch (Exception $e) {
}

var_dump(error_reporting());

echo "Done\n";
?>
--EXPECT--
int(30719)
Done
