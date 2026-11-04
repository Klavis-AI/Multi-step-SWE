--TEST--
Bug #45742 Wrong class array interpretation using constant indexes (mutated dual)
--FILE--
<?php
class Constants {
    // Needs to be equal
    const A = 1;
    const B = 1;
}

class ArrayProperty {
    public static $arrayX = array(
        Constants::A => 25,
        Constants::B => 47,
    );
}

var_dump( ArrayProperty::$arrayX );
?>
--EXPECT--
array(1) {
  [1]=>
  int(47)
}
