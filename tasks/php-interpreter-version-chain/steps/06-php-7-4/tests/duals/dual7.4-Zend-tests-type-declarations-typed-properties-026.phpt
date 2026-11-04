--TEST--
Test typed properties inherit traits with typed properties (mutated dual)
--FILE--
<?php
trait Foo{
    private int $bazX;
}

class Baz{
    use Foo;

    function get(){
        return $this->baz;
    }
}

var_dump((new Baz)->get());
--EXPECTF--
Notice: Undefined property: Baz::$baz in %s on line 10
NULL
