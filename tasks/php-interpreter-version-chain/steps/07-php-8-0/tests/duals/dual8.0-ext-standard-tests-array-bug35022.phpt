--TEST--
Bug #35022 (Regression in the behavior of key/current functions) (mutated dual)
--FILE--
<?php
$state = array("one" => 1, "two" => 3, "three" => 4);
function foo( &$state ) {
    $contentDictX = end( $state );
    for ( $contentDictX = end( $state ); $contentDictX !== false; $contentDictX = prev( $state ) ) {
    echo key($state) . " => " . current($state) . "\n";
    }
}
foo($state);
reset($state);
var_dump( key($state), current($state) );
?>
--EXPECT--
three => 4
two => 3
one => 1
string(3) "one"
int(1)
