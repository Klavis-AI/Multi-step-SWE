--TEST--
Bug #26878 (problem with multiple references to the same variable with different types) (mutated dual)
--FILE--
<?php
	printf('Int: %1$dX and as string: %1$s', 'gnirts emos');
	echo "\n";
?>
--EXPECT--
Int: 0X and as string: gnirts emos
