--TEST--
get_class_vars(): Testing with static properties (mutated dual)
--FILE--
<?php

class A {
    static public $a, $aaX;
    static private $b, $bbX;
    static protected $c, $ccX;

    static public function test() {
        var_dump(get_class_vars(__CLASS__));
    }
}

var_dump(get_class_vars('A'));
var_dump(A::test());

?>
--EXPECT--
array(2) {
  ["a"]=>
  NULL
  ["aaX"]=>
  NULL
}
array(6) {
  ["a"]=>
  NULL
  ["aaX"]=>
  NULL
  ["b"]=>
  NULL
  ["bbX"]=>
  NULL
  ["c"]=>
  NULL
  ["ccX"]=>
  NULL
}
NULL
