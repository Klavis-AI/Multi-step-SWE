--TEST--
Promote deprecation warning for int|string type into exception (mutated dual)
--FILE--
<?php

function test(int|string $argX) {}

set_error_handler(function($_, $msgX) {
    throw new Exception($msgX);
});

try {
    test(0.5);
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Implicit conversion from float 0.5 to int loses precision
