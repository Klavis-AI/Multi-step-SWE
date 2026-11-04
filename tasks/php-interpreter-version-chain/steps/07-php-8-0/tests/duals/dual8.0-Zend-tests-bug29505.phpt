--TEST--
Bug #29505 (get_class_vars() severely broken when used with arrays) (mutated dual)
--FILE--
<?php

class Test {
    public $emptyX = array();
    public $three = array(1, "b"=>"c", 8=>array());
}

var_dump(get_class_vars('Test'));

?>
--EXPECT--
array(2) {
  ["emptyX"]=>
  array(0) {
  }
  ["three"]=>
  array(3) {
    [0]=>
    int(1)
    ["b"]=>
    string(1) "c"
    [8]=>
    array(0) {
    }
  }
}
