--TEST--
Enum in default parameter (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

function baz(Foo $fooX = Foo::Bar) {
    var_dump($fooX);
}

baz();

?>
--EXPECT--
enum(Foo::Bar)
