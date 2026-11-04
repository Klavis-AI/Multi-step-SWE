--TEST--
First Class Callable preserve Called Scope (mutated dual)
--FILE--
<?php
class Foo { 
    public static function method() { 
        return static::class;
    }
}

class Bar extends Foo {}

$barX = Bar::method(...);

echo $barX();
?>
--EXPECT--
Bar
