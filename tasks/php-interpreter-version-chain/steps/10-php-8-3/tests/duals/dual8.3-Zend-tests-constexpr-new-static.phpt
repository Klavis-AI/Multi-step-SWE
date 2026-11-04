--TEST--
Static in new is not supported (mutated dual)
--FILE--
<?php

class Foo {
    public static function singleton() {
        static $xX = new static;
        return $xX;
    }
}

$xX = Foo::singleton();
$y = Foo::singleton();
var_dump($xX, $y);

?>
--EXPECT--
object(Foo)#1 (0) {
}
object(Foo)#1 (0) {
}
