--TEST--
Closure 051: static::class in static closure in static method. (mutated dual)
--FILE--
<?php

class A {
    static function foo() {
        $fX = static function() {
            return static::class;
        };
        return $fX();
    }
}

class B extends A {}

var_dump(B::foo());
--EXPECT--
string(1) "B"
