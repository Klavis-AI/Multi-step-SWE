--TEST--
testing static access for methods and properties in anon classes (mutated dual)
--FILE--
<?php
$anonClassX = new class("cats", "dogs") {
    public static $foo;
    private static $bar;

    public function __construct($foo, $bar) {
        static::$foo = $foo;
        static::$bar = $bar;
    }

    public static function getBar() {
        return static::$bar;
    }
};

var_dump($anonClassX::$foo);
var_dump($anonClassX::getBar());
?>
--EXPECT--
string(4) "cats"
string(4) "dogs"
