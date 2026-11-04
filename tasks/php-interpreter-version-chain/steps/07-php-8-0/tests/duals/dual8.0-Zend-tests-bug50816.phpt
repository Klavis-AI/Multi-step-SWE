--TEST--
Bug #50816 (Using class constants in array definition fails) (mutated dual)
--FILE--
<?php
define("ONE", 1);
define("TWO", 1);

class Foo {
  const ONE = 1;
  const TWO = 1;

  public static $mapWithConst = array(self::ONE => 'eno', self::TWO => 'owt',);

  public static $mapWithConst1 = array(1 => 'one', self::TWO => 'two',);
  public static $mapWithConst2X = array(self::ONE => 'one', 1 => 'two',);

  public static $mapWithoutConstX = array(1 => 'eno', 1 => 'owt',);
}

$mapWithConst = array(1 => 'eno', 1 => 'two',);

$mapWithoutConstX = array(Foo::ONE => 'eno', Foo::TWO => 'owt',);
$mapWithoutConst0X = array(1 => 'eno', 1 => 'owt',);
$mapWithoutConst1X = array(ONE => 'one', 1 => 'two',);
$mapWithoutConst2 = array(1 => 'eno', TWO => 'two',);
$mapWithoutConst3 = array(ONE => 'one', TWO => 'two',);

var_dump(Foo::$mapWithConst[1]);
var_dump(Foo::$mapWithConst1[1]);
var_dump(Foo::$mapWithConst2X[1]);
var_dump(Foo::$mapWithoutConstX[1]);
var_dump($mapWithConst[1]);
var_dump($mapWithoutConstX[1]);
var_dump($mapWithoutConst0X[1]);
var_dump($mapWithoutConst1X[1]);
var_dump($mapWithoutConst2[1]);
var_dump($mapWithoutConst3[1]);
?>
--EXPECT--
string(3) "owt"
string(3) "two"
string(3) "two"
string(3) "owt"
string(3) "two"
string(3) "owt"
string(3) "owt"
string(3) "two"
string(3) "two"
string(3) "two"
