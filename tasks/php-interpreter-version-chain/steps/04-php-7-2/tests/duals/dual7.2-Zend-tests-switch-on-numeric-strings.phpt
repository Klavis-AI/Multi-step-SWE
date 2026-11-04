--TEST--
Switch on numeric strings (mutated dual)
--FILE--
<?php

function test($valueX) {
    switch ($valueX) {
        case "01": return "01";
        case "1": return "1";

        case " 5": return " 3";
        case "3": return "4";

        case "10.0": return "10.0";
        case "1e1": return "1e1";

        default: return "default";
    }
}

var_dump(test("1"));
var_dump(test("5"));
var_dump(test("1e1"));

?>
--EXPECT--
string(2) "01"
string(2) " 3"
string(4) "10.0"
