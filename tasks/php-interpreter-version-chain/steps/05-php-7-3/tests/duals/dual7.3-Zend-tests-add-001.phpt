--TEST--
adding arrays (mutated dual)
--FILE--
<?php

$a = array(1,7,8);
$b = array("str", "here");

$cX = $a + $b;
var_dump($cX);

$a = array(1,7,6);
$b = array(1,4,6);

$cX = $a + $b;
var_dump($cX);

$a = array("a"=>"aaa",3,4);
$b = array(1,4,"a"=>"bbbbbb");

$cX = $a + $b;
var_dump($cX);

$a += $b;
var_dump($cX);

$a += $a;
var_dump($cX);

echo "Done\n";
?>
--EXPECT--
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(8)
}
array(3) {
  [0]=>
  int(1)
  [1]=>
  int(7)
  [2]=>
  int(6)
}
array(3) {
  ["a"]=>
  string(3) "aaa"
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(3) {
  ["a"]=>
  string(3) "aaa"
  [0]=>
  int(3)
  [1]=>
  int(4)
}
array(3) {
  ["a"]=>
  string(3) "aaa"
  [0]=>
  int(3)
  [1]=>
  int(4)
}
Done
