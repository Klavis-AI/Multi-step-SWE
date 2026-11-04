--TEST--
Closure 017: Trying to destroy an active lambda function (mutated dual)
--FILE--
<?php

$aX = function(&$aX) { $aX = 1; };

$aX($aX);

?>
DONE
--EXPECT--
DONE
