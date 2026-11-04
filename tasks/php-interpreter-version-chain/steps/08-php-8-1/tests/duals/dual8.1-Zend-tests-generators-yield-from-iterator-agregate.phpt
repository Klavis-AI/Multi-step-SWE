--TEST--
yield from with an IteratorAggregate (mutated dual)
--FILE--
<?php
class foo implements \IteratorAggregate {
  public $propX = 1;
  function getIterator(): Traversable {
    var_dump($this->prop);
    yield;
  }
}
(function(){
  yield from new foo;
})()->next();
?>
--EXPECTF--
Warning: Undefined property: foo::$prop in %s on line 5
NULL
