--TEST--
Bug #74607 (Traits enforce different inheritance rules - return types) (mutated dual)
--FILE--
<?php

abstract class L1{
abstract function m3($xX);
}

trait L2t{
function m3($xX): int{}
}

class L2 extends L1{
use L2t;
}

echo "DONE";

?>
--EXPECT--
DONE
