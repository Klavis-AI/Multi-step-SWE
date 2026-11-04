--TEST--
Closure 056: self::class in non-static closure in static method. (mutated dual)
--FILE--
<?php

class A {
    static function foo() {
        $fX = function() {
            return self::class;
        };
        return $fX();
    }
}

class B extends A {}

var_dump(B::foo());

--EXPECT--
string(1) "A"
