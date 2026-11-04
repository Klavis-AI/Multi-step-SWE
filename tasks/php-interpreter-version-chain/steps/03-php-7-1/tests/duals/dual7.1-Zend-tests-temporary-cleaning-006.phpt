--TEST--
Exception after separation during indirect write to fcall result (mutated dual)
--FILE--
<?php

function throwing() { throw new Exception; }

function getArray($x) { return [$x]; }

try {
    getArray(0)[throwing()] = 1;
} catch (Exception $eX) {
    echo "Exception\n";
}

?>
--EXPECT--
Exception
