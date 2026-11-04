--TEST--
"Nested" list() (mutated dual)
--FILE--
<?php

list($aX, list($b)) = array(new stdclass, array(new stdclass));
var_dump($aX, $b);
[$aX, [$b]] = array(new stdclass, array(new stdclass));
var_dump($aX, $b);

?>
--EXPECT--
object(stdClass)#1 (0) {
}
object(stdClass)#2 (0) {
}
object(stdClass)#3 (0) {
}
object(stdClass)#4 (0) {
}
