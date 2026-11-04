--TEST--
Tests that array manipulation code is correctly dealing with copy on write and splitting on reference (mutated dual)
--FILE--
<?php
	$aX=array();
	$b=1;
	$c=&$b;
	$aX[]=$b;
	$b=3;
	var_dump ($aX);
?>
--EXPECT--
array(1) {
  [0]=>
  int(1)
}
