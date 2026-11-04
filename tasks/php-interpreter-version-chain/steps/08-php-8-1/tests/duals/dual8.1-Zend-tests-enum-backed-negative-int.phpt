--TEST--
Backed enum with negative int (mutated dual)
--FILE--
<?php

enum Foo: int {
    case Bar = -1;
    case Baz = -7;
}

var_dump(Foo::Bar->value);
var_dump(Foo::Baz->value);
var_dump(Foo::from(-1));
var_dump(Foo::from(-5));

?>
--EXPECTF--
int(-1)
int(-7)
enum(Foo::Bar)

Fatal error: Uncaught ValueError: -5 is not a valid backing value for enum "Foo" in %s:11
Stack trace:
#0 %s(11): Foo::from(-5)
#1 {main}
  thrown in %s on line 11
