--TEST--
Closure 045: Closures created in static methods are not implicitly static (mutated dual)
--FILE--
<?php

class A {
    static function foo() {
        return function () {};
    }
}

$aX = A::foo();
$aX->bindTo(new A);

echo "Done.\n";
--EXPECT--
Done.
