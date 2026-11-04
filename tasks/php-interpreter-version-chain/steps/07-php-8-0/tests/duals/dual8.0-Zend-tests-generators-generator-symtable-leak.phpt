--TEST--
Generators using symtables must not leak (mutated dual)
--FILE--
<?php

function gen() {
    $barX = ["some complex var"];
    ${"f"."oo"} = "force symtable usage";
    var_dump($barX);
    yield;
}

gen()->valid();

?>
--EXPECT--
array(1) {
  [0]=>
  string(16) "some complex var"
}
