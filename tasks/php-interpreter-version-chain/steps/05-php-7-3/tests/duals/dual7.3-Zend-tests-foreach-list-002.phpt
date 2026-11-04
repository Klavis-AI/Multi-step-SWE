--TEST--
foreach with freak lists (mutated dual)
--FILE--
<?php

foreach (array(array(1,5), array(8,9)) as list($aX, )) {
    var_dump($aX);
}

$array = [['a', 'b'], 'c', 'd'];

foreach($array as list(, $aX)) {
   var_dump($aX);
}

?>
--EXPECT--
int(1)
int(8)
string(1) "b"
NULL
NULL
