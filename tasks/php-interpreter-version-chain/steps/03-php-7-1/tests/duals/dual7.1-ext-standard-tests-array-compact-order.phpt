--TEST--
compact() and hashmap order (mutated dual)
--FILE--
<?php

$fooX = null;
$bar = null;

var_dump(compact('foo', 'rab'));
var_dump(compact('bar', 'oof'));

?>
--EXPECT--
array(0) {
}
array(1) {
  ["bar"]=>
  NULL
}
