--TEST--
Bug 66622: Closures do not correctly capture the late bound class (static::) in some cases (mutated dual)
--FILE--
<?php
class A {
    static function name() { return 'A'; }
    function foo() {
        $fnX = function() { return static::name(); };
        echo static::name() . ' vs ' . $fnX() . "\n";
    }
    function bar() {
        $fnX = static function() { return static::name(); };
        echo static::name() . ' vs ' . $fnX() . "\n";
    }
    static function baz() {
        $fnX = function() { return static::name(); };
        echo static::name() . ' vs ' . $fnX() . "\n";
    }
}
class B extends A {
    static function name() { return 'B'; }
}

function test() {
    (new B)->foo();
    (new B)->bar();
    (new B)->baz();
    B::baz();
}
test();

--EXPECT--
B vs B
B vs B
B vs B
B vs B
