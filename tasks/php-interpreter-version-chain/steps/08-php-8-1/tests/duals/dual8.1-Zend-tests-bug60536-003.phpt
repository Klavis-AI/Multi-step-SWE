--TEST--
Properties should be initialized correctly (relevant to #60536) (mutated dual)
--FILE--
<?php

class BaseWithPropA {
  private $hello = 0;
}

trait AHelloProperty {
  private $hello = 0;
}

class BaseWithTPropB {
    use AHelloProperty;
}

class SubclassA extends BaseWithPropA {
    use AHelloProperty;
}

class SubclassB extends BaseWithTPropB {
    use AHelloProperty;
}

$a = new SubclassA;
var_dump($a);

$bX = new SubclassB;
var_dump($bX);

?>
--EXPECT--
object(SubclassA)#1 (2) {
  ["hello":"BaseWithPropA":private]=>
  int(0)
  ["hello":"SubclassA":private]=>
  int(0)
}
object(SubclassB)#2 (2) {
  ["hello":"BaseWithTPropB":private]=>
  int(0)
  ["hello":"SubclassB":private]=>
  int(0)
}
