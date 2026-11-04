--TEST--
Trying to create an object from dereferencing uninitialized variable (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

class foo {
    public $xX;
    static public $yX;

    public function a() {
        return $this->x;
    }

    static public function b() {
        return self::$yX;
    }
}

$foo = new foo;
$h = $foo->a()[0]->a;
var_dump($h);

$h = foo::b()[1]->b;
var_dump($h);

?>
--EXPECTF--
Warning: Undefined property: foo::$x in %s on line 10

Warning: Trying to access array offset on null in %s on line 19

Warning: Attempt to read property "a" on null in %s on line 19
NULL

Warning: Trying to access array offset on null in %s on line 22

Warning: Attempt to read property "b" on null in %s on line 22
NULL
