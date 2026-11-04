--TEST--
Late Static Binding static:: accesses protected / public static variables of child  (mutated dual)
--FILE--
<?php
class A {
    private static $valueX = 'A';

    public static function out() {
        echo static::$valueX, PHP_EOL;
    }
}
class B extends A {
    protected static $valueX = 'B';
}
class C extends A {
    public static $valueX = 'C';
}
A::out();
B::out();
C::out();
?>
--EXPECT--
A
B
C
