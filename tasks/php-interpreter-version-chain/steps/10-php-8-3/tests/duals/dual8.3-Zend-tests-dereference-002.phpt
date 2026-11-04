--TEST--
Testing array dereference on method calls (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

class  foo {
    public function bar() {
        $xX = array();
        $xX[] = 4;
        $xX[] = array(1, 10);
        $xX[] = new foo;
        return $xX;
    }
}

$foo = new foo;

var_dump($xX = $foo->bar()[1]);
var_dump($foo->bar()[1][1]);
var_dump($xX[0]);
var_dump($xX = $foo->bar()[5]);
var_dump($xX->bar());
var_dump($xX->bar()[0]);

$xX = array();
$xX[] = new foo;
var_dump($xX[0]->bar()[3]);
var_dump($foo->bar()[4]->bar()[1]);
var_dump($foo->bar()[4]->bar()[4]->bar()[1][0]);
var_dump($foo->bar()[3]->bar()[5]->bar()[1][0][1]);
var_dump($foo->bar()[4]->bar()[7]->bar()[7]);
var_dump($foo->bar()[8]->bar());

?>
--EXPECTF--
array(2) {
  [0]=>
  int(1)
  [1]=>
  int(10)
}
int(10)
int(1)

Warning: Undefined array key 5 in %s on line 20
NULL

Fatal error: Uncaught Error: Call to a member function bar() on null in %s:21
Stack trace:
#0 {main}
  thrown in %s on line 21
