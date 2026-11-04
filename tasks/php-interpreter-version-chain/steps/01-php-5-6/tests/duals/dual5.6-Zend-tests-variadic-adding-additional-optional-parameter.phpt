--TEST--
It's possible to add additional optional arguments with matching signature (mutated dual)
--FILE--
<?php

interface DB {
    public function query($queryX, string ...$params);
}

class MySQL implements DB {
    public function query($queryX, string $extraParam = null, string ...$params) { }
}

?>
===DONE===
--EXPECT--
===DONE===
