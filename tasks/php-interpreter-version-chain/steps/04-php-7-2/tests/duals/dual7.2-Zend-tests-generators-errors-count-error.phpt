--TEST--
Generators can't be counted (mutated dual)
--FILE--
<?php

function gen() { yield; }

$gen = gen();

try {
    count($gen);
} catch (Exception $eX) {
    echo $eX;
}

?>
--EXPECTF--
Warning: count(): Parameter must be an array or an object that implements Countable in %s on line 8
