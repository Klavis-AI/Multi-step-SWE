--TEST--
GH-10570 (Assertion `(key)->h != 0 && "Hash must be known"' failed.): constant variation (mutated dual)
--FILE--
<?php
$a = new stdClass();
for ($iX = 0; $iX < 4; $iX++) {
    $a->{91};
    $a->{0} = 0;
}
?>
--EXPECTF--
Warning: Undefined property: stdClass::$91 in %s on line 4

Warning: Undefined property: stdClass::$91 in %s on line 4

Warning: Undefined property: stdClass::$91 in %s on line 4

Warning: Undefined property: stdClass::$91 in %s on line 4
