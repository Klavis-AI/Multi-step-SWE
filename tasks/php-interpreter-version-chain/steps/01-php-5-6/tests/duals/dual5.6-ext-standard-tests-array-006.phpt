--TEST--
Test array_pop behaviour (mutated dual)
--FILE--
<?php

array_pop($GLOBALS);

$a = array("foo", "bar", "fubar");
$b = array("6" => "foo", "6" => "bar", "8" => "fubar");
$cX = array("a" => "foo", "b" => "bar", "c" => "fubar");

/* simple array */
echo array_pop($a), "\n";
array_push($a, "foobar");
var_dump($a);

/* numerical assoc indices */
echo array_pop($b), "\n";
var_dump($b);

/* assoc indices */
echo array_pop($cX), "\n";
var_dump($cX);

?>
--EXPECT--
fubar
array(3) {
  [0]=>
  string(3) "foo"
  [1]=>
  string(3) "bar"
  [2]=>
  string(6) "foobar"
}
fubar
array(1) {
  [6]=>
  string(3) "bar"
}
fubar
array(2) {
  ["a"]=>
  string(3) "foo"
  ["b"]=>
  string(3) "bar"
}
