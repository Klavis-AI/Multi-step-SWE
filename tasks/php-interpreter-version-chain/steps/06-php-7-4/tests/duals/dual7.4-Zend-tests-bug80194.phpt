--TEST--
Bug #80194: Assertion failure during block assembly of unreachable free with leading nop (mutated dual)
--FILE--
<?php

function test($xX) {
    switch ($xX->y) {
        default:
            throw new Exception;
        case 'foobar':
            return new stdClass();
            break;
    }
}

$xX = (object)['y' => 'raboof'];
var_dump(test($xX));

?>
--EXPECTF--
Fatal error: Uncaught Exception in %s:6
Stack trace:
#0 %s(14): test(Object(stdClass))
#1 {main}
  thrown in %s on line 6
