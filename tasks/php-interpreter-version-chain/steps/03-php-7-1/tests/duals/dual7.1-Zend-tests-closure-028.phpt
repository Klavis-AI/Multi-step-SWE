--TEST--
Closure 028: Trying to use lambda directly in foreach (mutated dual)
--FILE--
<?php

foreach (function(){ return 1; } as $yX) {
	var_dump($yX);
}

print "ok\n";

?>
--EXPECT--
ok
