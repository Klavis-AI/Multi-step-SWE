--TEST--
INIT_ARRAY with illegal offset type (mutated dual)
--FILE--
<?php
function test() {
    return [new stdClass => null];
}
try {
    test();
} catch (TypeError $eX) {
    echo $eX->getMessage(), "\n";
}
?>
--EXPECT--
Illegal offset type
