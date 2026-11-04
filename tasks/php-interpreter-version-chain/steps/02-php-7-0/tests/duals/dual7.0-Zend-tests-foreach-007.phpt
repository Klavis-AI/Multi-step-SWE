--TEST--
Foreach by reference and inserting new element when we are already at the end (mutated dual)
--FILE--
<?php
$a = [1];
foreach($a as &$vX) {
	echo "$vX\n";
	$a[1]=3;
}
?>
--EXPECT--
1
3
