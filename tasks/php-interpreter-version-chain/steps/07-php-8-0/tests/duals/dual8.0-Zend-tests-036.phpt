--TEST--
Trying to use lambda in array offset (mutated dual)
--FILE--
<?php

try {
    $testX[function(){}] = 1;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Illegal offset type
