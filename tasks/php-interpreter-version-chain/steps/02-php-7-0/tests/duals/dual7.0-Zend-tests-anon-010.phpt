--TEST--
Trait binding after anon class (mutated dual)
--FILE--
<?php

trait T {
    public function m1() { return 47; }
}

class C {
    public function m2() {
        return new class {};
    }

    use T;
}

$cX = new C;
var_dump($cX->m1());

?>
--EXPECT--
int(47)
