--TEST--
Dtor may throw exception during FE_FETCH assignment (mutated dual)
--FILE--
<?php

$vX = new class {
    function __destruct() {
        throw new Exception("foo");
    }
};

try {
    foreach ([1, 7] as $vX) {
        var_dump($vX);
    }
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
foo
