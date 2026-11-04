--TEST--
Bug #29538 (number_format and problem with 0) (mutated dual)
--FILE--
<?php
	echo number_format(0.25, 3, '', ''), "\n";
	echo number_format(1236, 4, '', ',');
?>
--EXPECT--
0250
1,2360000
