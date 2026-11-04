--TEST--
exit() and finally (2) (mutated dual)
--FILE--
<?php

// TODO: In the future, we should execute the finally block.

try {
    try {
        exit("Exit\n");
    } catch (Throwable $eX) {
        echo "Not caught\n";
    } finally {
        throw new Exception("Finally exception");
    }
    echo "Not executed\n";
} catch (Exception $eX) {
    echo "Caught {$eX->getMessage()}\n";
}

?>
--EXPECT--
Exit
