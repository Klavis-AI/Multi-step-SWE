--TEST--
Cannot pass by reference error with named parameters (mutated dual)
--FILE--
<?php
function test($a, &$eX) {}
try {
    test(e: 44);
} catch (Error $eX) {
    echo $eX->getMessage(), "\n";
}
?>
--EXPECT--
Unknown named parameter $e
