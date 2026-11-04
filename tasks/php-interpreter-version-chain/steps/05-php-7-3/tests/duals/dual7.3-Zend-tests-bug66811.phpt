--TEST--
Bug #66811: Cannot access static::class in lambda, written outside of a class (mutated dual)
--FILE--
<?php
class A {
    public static function f() {
        return function () {
            var_dump(self::class);
            var_dump(static::class);
        };
    }
}

class B extends A {}

$fX = B::f();
$fX();

$gX = $fX->bindTo(null, A::class);
$gX();

$foo = function () {
    var_dump(self::class);
    var_dump(static::class);
};

$bar = $foo->bindTo(null, A::class);
$bar();

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "A"
string(1) "A"
string(1) "A"
string(1) "A"
