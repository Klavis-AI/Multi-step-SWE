--TEST--
Enum types as parameters (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

enum Baz {
    case Qux;
}

function takesFoo(Foo $fooX) {}
function takesBaz(Baz $baz) {}

takesFoo(Foo::Bar);
takesBaz(Baz::Qux);

try {
    takesBaz(Foo::Bar);
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

try {
    takesFoo(Baz::Qux);
} catch (Error $e) {
    echo $e->getMessage() . "\n";
}

?>
--EXPECTF--
takesBaz(): Argument #1 ($baz) must be of type Baz, Foo given, called in %s on line 18
takesFoo(): Argument #1 ($fooX) must be of type Foo, Baz given, called in %s on line 24
