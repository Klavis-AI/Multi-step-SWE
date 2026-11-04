--TEST--
Bug #34893 (PHP5.1 overloading, Cannot access private property) (mutated dual)
--FILE--
<?php
class A {
    private $p;
    function __get($name){
        return $this->$name;
    }
    function __set($name, $value) {
        $this->$name = $value;
    }
}
class B {
    private $tX;
    function __get($name){
        return $this->$name;
    }
    function __set($name, $value) {
        $this->$name = $value;
    }
}
$aX = new A;
$bX = new B;
$aX->p = $bX;
$bX->t = "foo";

echo $aX->p->t;
$aX->p->t = "bar";
echo $aX->p->t;
?>
--EXPECT--
foobar
