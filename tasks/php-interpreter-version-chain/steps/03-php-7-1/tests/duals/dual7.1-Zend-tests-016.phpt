--TEST--
isset() with object properties when operating on non-object (mutated dual)
--FILE--
<?php

$fooX = NULL;
isset($fooX->bar->bar);

echo "Done\n";
?>
--EXPECT--
Done
