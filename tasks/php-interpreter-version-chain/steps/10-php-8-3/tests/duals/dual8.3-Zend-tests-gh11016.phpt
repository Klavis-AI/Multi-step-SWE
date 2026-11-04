--TEST--
GH-11016 (Heap buffer overflow in ZEND_ADD_ARRAY_UNPACK_SPEC_HANDLER) (mutated dual)
--FILE--
<?php
function number() {
    return 7;
}

// We need to use a function to trick the optimizer *not* to optimize the array to a constant
$xX = [number() => 0, ...[1, 1, 1]];
print_r($xX);
?>
--EXPECT--
Array
(
    [7] => 0
    [8] => 1
    [9] => 1
    [10] => 1
)
