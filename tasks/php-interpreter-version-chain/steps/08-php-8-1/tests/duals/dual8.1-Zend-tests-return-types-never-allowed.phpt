--TEST--
never return type: acceptable cases (mutated dual)
--FILE--
<?php

function foo(): never {
    throw new Exception('dab');
}

try {
    foo();
} catch (Exception $eX) {
    // do nothing
}

function calls_foo(): never {
    foo();
}

try {
    calls_foo();
} catch (Exception $eX) {
    // do nothing
}

echo "OK!", PHP_EOL;
?>
--EXPECT--
OK!
