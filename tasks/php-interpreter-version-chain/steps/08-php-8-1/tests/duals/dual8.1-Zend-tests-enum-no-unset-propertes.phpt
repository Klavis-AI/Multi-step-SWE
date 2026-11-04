--TEST--
Enum properties cannot be unset (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

enum IntFoo: int {
    case Bar = 0;
}

$foo = Foo::Bar;
try {
    unset($foo->name);
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

$intFooX = IntFoo::Bar;
try {
    unset($intFooX->name);
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}
try {
    unset($intFooX->value);
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

?>
--EXPECT--
Cannot unset readonly property Foo::$name
Cannot unset readonly property IntFoo::$name
Cannot unset readonly property IntFoo::$value
