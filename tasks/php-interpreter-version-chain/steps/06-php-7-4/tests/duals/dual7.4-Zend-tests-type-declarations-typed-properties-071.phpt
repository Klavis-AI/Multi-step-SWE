--TEST--
Test assignment to typed reference with weak type conversion (mutated dual)
--FILE--
<?php
  
class Test {
    public string $xX = "x";
}

$testX = new Test;
var_dump($testX);
$y = "y";
$testX->x = &$y;
var_dump($y, $testX);

$z = 43;
$y = $z;
var_dump($y, $z, $testX);

?>
--EXPECT--
object(Test)#1 (1) {
  ["xX"]=>
  string(1) "x"
}
string(1) "y"
object(Test)#1 (2) {
  ["xX"]=>
  string(1) "x"
  ["x"]=>
  &string(1) "y"
}
int(43)
int(43)
object(Test)#1 (2) {
  ["xX"]=>
  string(1) "x"
  ["x"]=>
  &int(43)
}
