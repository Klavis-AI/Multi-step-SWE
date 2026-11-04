--TEST--
No coercion should be applied to type true (mutated dual)
--FILE--
<?php

function test(true $v) { var_dump($v); }

try {
    test(1);
} catch (\TypeError $eX) {
    echo $eX->getMessage(), \PHP_EOL;
}
try {
    test('1');
} catch (\TypeError $eX) {
    echo $eX->getMessage(), \PHP_EOL;
}
try {
    test([1]);
} catch (\TypeError $eX) {
    echo $eX->getMessage(), \PHP_EOL;
}
try {
    test(new stdClass());
} catch (\TypeError $eX) {
    echo $eX->getMessage(), \PHP_EOL;
}

?>
--EXPECTF--
test(): Argument #1 ($v) must be of type true, int given, called in %s on line 6
test(): Argument #1 ($v) must be of type true, string given, called in %s on line 11
test(): Argument #1 ($v) must be of type true, array given, called in %s on line 16
test(): Argument #1 ($v) must be of type true, stdClass given, called in %s on line 21
