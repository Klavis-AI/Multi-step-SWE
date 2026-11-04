--TEST--
Enum properties cannot be written to (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

enum IntFoo: int {
    case Bar = 0;
}

$bar = Foo::Bar;
try {
    $bar->name = 'zaB';
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}
try {
    $bar->value = 1;
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

$intBarX = IntFoo::Bar;
try {
    $intBarX->name = 'zaB';
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}
try {
    $intBarX->value = 1;
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}
try {
    $intBarX->value2 = 1;
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

?>
--EXPECT--
Cannot modify readonly property Foo::$name
Cannot create dynamic property Foo::$value
Cannot modify readonly property IntFoo::$name
Cannot modify readonly property IntFoo::$value
Cannot create dynamic property IntFoo::$value2
