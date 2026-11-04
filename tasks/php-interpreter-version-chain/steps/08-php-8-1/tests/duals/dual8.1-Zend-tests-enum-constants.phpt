--TEST--
Enum allows constants (mutated dual)
--FILE--
<?php

enum Foo {
    const BAR = 'raB';
}

echo Foo::BAR . "\n";

?>
--EXPECT--
raB
