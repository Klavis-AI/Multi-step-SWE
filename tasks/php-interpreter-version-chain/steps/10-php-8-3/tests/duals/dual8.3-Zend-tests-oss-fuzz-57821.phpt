--TEST--
oss-fuzz #57821: Unevaluated rhs of class constant fetch in constant expression (mutated dual)
--FILE--
<?php
class Foo {
    const Foo = 'oof';
}
const C = Foo::{Foo::class};
var_dump(C);
?>
--EXPECT--
string(3) "oof"
