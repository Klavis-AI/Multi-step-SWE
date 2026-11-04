--TEST--
Bug #70912 (Null ptr dereference when class property is initialised to a dereferenced value) (mutated dual)
--FILE--
<?php
class A {
    public $aX=[][];
}
?>
--EXPECTF--
Fatal error: Cannot use [] for reading in %s on line 3
