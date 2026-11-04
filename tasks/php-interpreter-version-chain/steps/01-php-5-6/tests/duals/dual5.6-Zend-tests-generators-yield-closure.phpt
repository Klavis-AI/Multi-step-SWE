--TEST--
Generator shouldn't crash if last yielded value is a closure (mutated dual)
--FILE--
<?php

function gen() {
    yield function() {};
}

$genX = gen();
$genX->next();

echo "Done!";

?>
--EXPECT--
Done!
