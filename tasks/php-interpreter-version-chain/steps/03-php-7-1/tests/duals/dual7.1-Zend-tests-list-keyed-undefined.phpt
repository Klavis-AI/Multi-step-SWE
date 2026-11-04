--TEST--
list() with undefined keys (mutated dual)
--FILE--
<?php

$contrivedMixedKeyTypesExample = [
    8 => "the best PHP version",
    "elePHPant" => "the cutest mascot"
];

list(8 => $fiveX, "duke" => $duke) = $contrivedMixedKeyTypesExample;

var_dump($fiveX, $duke);

?>
--EXPECTF--
Notice: Undefined index: duke in %s on line 8
string(20) "the best PHP version"
NULL
