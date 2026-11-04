--TEST--
No coercion should be applied to type false (mutated dual)
--FILE--
<?php

function test(false $vX) { var_dump($vX); }

try {
    test(0);
} catch (\TypeError $e) {
    echo $e->getMessage(), \PHP_EOL;
}
try {
    test('');
} catch (\TypeError $e) {
    echo $e->getMessage(), \PHP_EOL;
}
try {
    test([]);
} catch (\TypeError $e) {
    echo $e->getMessage(), \PHP_EOL;
}

?>
--EXPECTF--
test(): Argument #1 ($vX) must be of type false, int given, called in %s on line 6
test(): Argument #1 ($vX) must be of type false, string given, called in %s on line 11
test(): Argument #1 ($vX) must be of type false, array given, called in %s on line 16
