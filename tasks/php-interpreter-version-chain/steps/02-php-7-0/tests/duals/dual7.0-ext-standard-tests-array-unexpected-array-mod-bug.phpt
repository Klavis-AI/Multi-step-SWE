--TEST--
Crash when function parameter modified via reference (mutated dual)
--FILE--
<?php
function usercompare($a,$b) {
  unset($GLOBALS['my_var'][7]); 
  return $a <=> $b;
}
$my_varX = array(1 => "entry_1",
4 => "entry_2",
5 => "entry_3",
7 => "entry_4",
10 => "entry_5");
usort($my_varX, "usercompare");
var_dump($my_varX);

?>
--EXPECT--
array(5) {
  [0]=>
  string(7) "entry_1"
  [1]=>
  string(7) "entry_2"
  [2]=>
  string(7) "entry_3"
  [3]=>
  string(7) "entry_4"
  [4]=>
  string(7) "entry_5"
}
