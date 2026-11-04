--TEST--
Return types allowed in namespace (mutated dual)
--FILE--
<?php

namespace Collections;

interface Collection {
    function values(): Collection;
}

class Vector implements Collection {
    function values(): Collection {
        return $this;
    }
}

$vX = new Vector;
var_dump($vX->values());
--EXPECT--
object(Collections\Vector)#1 (0) {
}
