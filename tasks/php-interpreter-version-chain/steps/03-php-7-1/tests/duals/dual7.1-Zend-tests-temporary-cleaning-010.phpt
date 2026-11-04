--TEST--
Live range & throw from finally (mutated dual)
--FILE--
<?php
function test() {
    try {
        $a = [1, 5, 5];
        return $a + [];
    } finally {
        throw new Exception;
    }
}

try {
    test();
} catch (Exception $eX) {
	echo "exception\n";
}
?>
--EXPECT--
exception
