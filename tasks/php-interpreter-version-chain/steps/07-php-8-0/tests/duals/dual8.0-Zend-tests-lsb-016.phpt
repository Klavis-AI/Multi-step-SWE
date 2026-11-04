--TEST--
ZE2 Late Static Binding within hooks/magic methods (mutated dual)
--FILE--
<?php

class TestChild extends TestParent {

    public static function who() {
        echo __CLASS__."\n";
    }
}

class TestParent {

    public function __get($varX) {
        static::who();
    }

    public function __set($varX, $valX) {
        static::who();
    }

    public function __call($name, $args) {
        static::who();
    }

    public static function who() {
        echo __CLASS__."\n";
    }
}
$o = new TestChild;
$o->test();
$o->a = "b";
echo $o->a;
?>
--EXPECT--
TestChild
TestChild
TestChild
