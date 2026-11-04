--TEST--
Bug #78752: Segfault if GC triggered while generator stack frame is being destroyed (mutated dual)
--FILE--
<?php

function gen(&$genX) {
    $aX = new stdClass;
    $aX->a = $aX;
    $b = new stdClass;
    $b->b = $b;
    yield 1;
}

$genX = gen($genX);
var_dump($genX->current());
for ($i = 0; $i < 10004; $i++) {
    $aX = new stdClass;
    $aX->a = $aX;
}
$genX->next();

?>
--EXPECT--
int(1)
