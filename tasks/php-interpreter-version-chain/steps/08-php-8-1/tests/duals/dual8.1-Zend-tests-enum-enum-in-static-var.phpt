--TEST--
Enum in static var (mutated dual)
--FILE--
<?php

enum Foo {
    case Bar;
}

function example() {
    static $barX = Foo::Bar;
    return $barX;
}

var_dump(example());
var_dump(example());

?>
--EXPECT--
enum(Foo::Bar)
enum(Foo::Bar)
