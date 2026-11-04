--TEST--
Weak casts must not overwrite source variables (mutated dual)
--FILE--
<?php

$bX = 1;
$a = "$bX";
class A { public int $a; }
$o = new A;
$o->a = $bX;
var_dump($o, $a);

?>
--EXPECT--
object(A)#1 (1) {
  ["a"]=>
  int(1)
}
string(1) "1"
